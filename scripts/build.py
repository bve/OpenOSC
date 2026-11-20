from pathlib import Path
import sys

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(root / "scripts"))
import board_config  # noqa: E402
import boot_splash  # noqa: E402
import generate_board_config  # noqa: E402
import vector_table  # noqa: E402

stale = generate_board_config.stale()
if stale:
    sys.exit("Generated board files are out of date; run scripts/generate_board_config.py: "
             + ", ".join(path.name for path in stale))
board = board_config.load(env.subst("$BOARD"))
image_kind = env.GetProjectOption("custom_openosc_image")
generated = Path(env.subst("$BUILD_DIR")) / "generated"
try:
    boot_splash.generate(root, generated / "boot_splash_images.hpp", image_kind == "application")
except (OSError, ValueError) as error:
    sys.exit(f"Boot splash: {error}")
env.Append(CPPPATH=[str(generated)])
env.Append(CPPDEFINES=[("OPENOSC_BOARD", board.model)])
include_dirs = [
    "include", "vendor/cmsis", "vendor/hal/Inc", "vendor/fatfs",
    "vendor/usb/Core/Inc", "vendor/usb/Class/MSC/Inc",
]
env.Append(CPPPATH=[str(root / path) for path in include_dirs])
env.Append(ASFLAGS=["-mfloat-abi=hard", "-mfpu=fpv5-d16"])
env.Append(CXXFLAGS=["-std=gnu++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"])
env.Append(LINKFLAGS=["-flto", "-nostartfiles", "-mfloat-abi=hard", "-mfpu=fpv5-d16",
                      "--specs=nano.specs", "--specs=nosys.specs",
                      "-Wl,-Map," + env.subst("$BUILD_DIR/firmware.map")])
env.Append(LIBPATH=[str(root / "ld")], LIBS=["c", "gcc", "nosys"])
for folder in ("vendor/hal/Src", "vendor/usb/Core/Src", "vendor/usb/Class/MSC/Src"):
    env.BuildSources(env.subst("$BUILD_DIR/") + folder, str(root / folder))
env.BuildSources(env.subst("$BUILD_DIR/vendor/fatfs"), str(root / "vendor/fatfs"),
                 src_filter=["+<ff.c>", "+<ffunicode.c>"])

if image_kind == "application":
    import cmsis_dsp  # noqa: E402
    env.Append(CPPPATH=[str(cmsis_dsp.INCLUDE)], CPPDEFINES=cmsis_dsp.DEFINES)
    dsp_root = root / "vendor/cmsis_dsp/Source"
    env.BuildSources(env.subst("$BUILD_DIR/vendor/cmsis_dsp"), str(dsp_root),
                     src_filter=["-<*>"] + [f"+<{p.relative_to(dsp_root).as_posix()}>" for p in cmsis_dsp.SOURCES])
    env.BoardConfig().update("upload.maximum_size", board.max_payload)
    env.Append(LINKFLAGS=[f"-Wl,--defsym=__openosc_flash_end=0x{board.flash_end:08X}"])
    env.Append(LIBS=["m"])
    version = (root / "VERSION").read_text().strip()
    env.Append(CPPDEFINES=[("OPENOSC_VERSION", '\\"' + version + '\\"')])

    def package_application(source, target, env):
        import importlib.util
        spec = importlib.util.spec_from_file_location("openosc_package", root / "scripts/package_image.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        image = Path(str(target[0]))
        vector_table.check(image.with_suffix(".elf"), image)
        output = image.with_name(module.image_name(board))
        marked = module.add_trailer(image.read_bytes(), board)
        image.write_bytes(marked)
        output.write_bytes(module.package(marked, version, board))
        print(f"Packaged {output.name}: {output.stat().st_size} bytes, version {version}")

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package_application)
elif image_kind == "recovery":
    import bin_to_ztk  # noqa: E402

    def package_recovery(source, target, env):
        image = Path(str(target[0]))
        vector_table.check(image.with_suffix(".elf"), image)
        model = bin_to_ztk.MODELS[str(board.model)]
        output = image.with_name(model.filename)
        container = bin_to_ztk.ZtkContainer.pack(image.read_bytes(), model)
        bin_to_ztk.ZtkContainer.unpack(container, model)
        output.write_bytes(container)
        print(f"Packaged {output.name}: {output.stat().st_size} bytes")

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package_recovery)

