"""Install the r17 terminal hotfix and PBR materials inside Unreal Editor 5.6.

Run this file through Tools -> Execute Python Script while the SML project is
open.  Do not run it as an operating-system Python script.

The filename is intentionally kept for compatibility with the r13 package.
This hotfix never deletes or clears material expressions. It also imports the
OBJ into a fresh staging mesh before copying LOD 0 into the stable mesh asset.
That avoids Unreal retaining an older five-slot material layout during reimport.
Every edited asset is persisted through EditorAssetLibrary; UE 5.6 does not
expose UObject.post_edit_change() on StaticMesh or Texture2D Python wrappers.
Only assets touched by this run are saved, avoiding a blocking recursive save
and validation of every abandoned staging folder.
"""

from __future__ import annotations

from pathlib import Path

import unreal


SCRIPT = Path(__file__).resolve()
SOURCE = SCRIPT.parent
if not (SOURCE / "SM_SFPPlannerTerminal_r2.obj").is_file():
    local_source = SCRIPT.parents[1] / "terminal-model"
    if local_source.is_dir():
        SOURCE = local_source

CONTENT_ROOT = "/SFPFactoryPlanner/Terminal"
MESH_ROOT = f"{CONTENT_ROOT}/Mesh"
PBR_ROOT = f"{MESH_ROOT}/PBR"
MAIN_MESH_PATH = f"{MESH_ROOT}/SM_SFPPlannerTerminal_r2.SM_SFPPlannerTerminal_r2"
SCREEN_MESH_PATH = f"{MESH_ROOT}/SM_SFPPlannerTerminalScreen_r3.SM_SFPPlannerTerminalScreen_r3"

SURFACES = ("Body", "Trim", "Accent", "Inset", "Metal", "Rubber")
SLOT_TO_SURFACE = {
    "SFP_R2_Body": "Body",
    "SFP_R2_Trim": "Trim",
    "SFP_R2_Accent": "Accent",
    "SFP_R2_Inset": "Inset",
    "SFP_R2_Metal": "Metal",
    "SFP_R2_Rubber": "Rubber",
}
SLOT_ORDER = (
    "SFP_R2_Rubber",
    "SFP_R2_Trim",
    "SFP_R2_Accent",
    "SFP_R2_Body",
    "SFP_R2_Inset",
    "SFP_R2_ScreenGlow",
    "SFP_R2_ScreenUI",
    "SFP_R2_Metal",
)


def require_sources() -> None:
    required = [
        SOURCE / "SM_SFPPlannerTerminal_r2.obj",
        SOURCE / "SM_SFPPlannerTerminalScreen_r3.obj",
        SOURCE / "T_SFPPlannerScreen.png",
        SOURCE / "T_PFP_TerminalIcon.png",
    ]
    required.extend(
        SOURCE / "PBR" / f"T_PFP_R13_{surface}_{kind}.png"
        for surface in SURFACES
        for kind in ("BaseColor", "Normal", "ORM")
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("r13 source files are missing:\n" + "\n".join(missing))


def import_task(
    filename: Path,
    destination: str,
    name: str,
    options=None,
    *,
    replace_existing: bool = True,
    replace_existing_settings: bool = False,
):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(filename))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace_existing)
    task.set_editor_property("replace_existing_settings", replace_existing_settings)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    expected = f"{destination}/{name}.{name}"
    asset = unreal.EditorAssetLibrary.load_asset(expected)
    if not asset:
        imported = task.get_editor_property("imported_object_paths")
        if imported:
            asset = unreal.EditorAssetLibrary.load_asset(imported[0])
    if not asset:
        raise RuntimeError(f"Import failed: {filename} -> {expected}")
    return asset


def import_texture(filename: Path, name: str, kind: str):
    destination = MESH_ROOT if kind in {"Screen", "Icon"} else PBR_ROOT
    texture = import_task(filename, destination, name)
    expected_srgb = kind in {"BaseColor", "Screen", "Icon"}
    texture.modify()
    texture.set_editor_property("srgb", expected_srgb)
    if kind == "Normal":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "ORM":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    if bool(texture.get_editor_property("srgb")) != expected_srgb:
        raise RuntimeError(f"Could not set the required sRGB state for {name}")
    if kind == "Normal" and texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
        raise RuntimeError(f"Could not set normal-map compression for {name}")
    if kind == "ORM" and texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
        raise RuntimeError(f"Could not set mask compression for {name}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, False):
        raise RuntimeError(f"Could not save configured texture {name}")
    unreal.log(f"Configured and saved texture {name}")
    return texture


def choose_generated_root() -> str:
    """Return an unused folder so a rerun never mutates partial assets."""
    names = [f"M_PFP_R17_{surface}" for surface in SURFACES]
    names.extend(("M_PFP_R17_ScreenGlow", "SM_PFP_R17_MainSource", "SM_PFP_R17_ScreenSource"))
    for index in range(1, 100):
        root = f"{PBR_ROOT}/Generated_r17_{index:02d}"
        occupied = any(
            unreal.EditorAssetLibrary.does_asset_exist(f"{root}/{name}.{name}")
            for name in names
        )
        if not occupied:
            unreal.EditorAssetLibrary.make_directory(root)
            return root
    raise RuntimeError("No unused r17 staging folder is available")


def make_material(name: str, material_root: str):
    path = f"{material_root}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError(f"Refusing to modify an existing material: {path}")
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name,
        material_root,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    if not material:
        raise RuntimeError(f"Could not create fresh material {path}")
    material.set_editor_property("two_sided", False)
    return material


def texture_parameter(material, texture, parameter: str, x: int, y: int, sampler_type=None):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionTextureSampleParameter2D,
        x,
        y,
    )
    expression.set_editor_property("parameter_name", parameter)
    expression.set_editor_property("texture", texture)
    if sampler_type is not None:
        expression.set_editor_property("sampler_type", sampler_type)
    return expression


def build_surface_material(surface: str, textures: dict[str, object], material_root: str):
    material = make_material(f"M_PFP_R17_{surface}", material_root)
    base = texture_parameter(material, textures["BaseColor"], "BaseColor", -720, -180)
    orm = texture_parameter(material, textures["ORM"], "ORM", -720, 80)
    normal = texture_parameter(
        material,
        textures["Normal"],
        "Normal",
        -720,
        340,
        unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
    )
    editing = unreal.MaterialEditingLibrary
    prop = unreal.MaterialProperty
    editing.connect_material_property(base, "RGB", prop.MP_BASE_COLOR)
    editing.connect_material_property(orm, "R", prop.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(orm, "G", prop.MP_ROUGHNESS)
    editing.connect_material_property(orm, "B", prop.MP_METALLIC)
    editing.connect_material_property(normal, "RGB", prop.MP_NORMAL)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    return material


def build_glow_material(material_root: str):
    material = make_material("M_PFP_R17_ScreenGlow", material_root)
    editing = unreal.MaterialEditingLibrary
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -520, -80)
    color.set_editor_property("parameter_name", "GlowColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.025, 0.55, 0.68, 1.0))
    strength = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -520, 100)
    strength.set_editor_property("parameter_name", "GlowStrength")
    strength.set_editor_property("default_value", 3.5)
    multiply = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -230, 20)
    editing.connect_material_expressions(color, "", multiply, "A")
    editing.connect_material_expressions(strength, "", multiply, "B")
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    return material


def get_screen_material():
    """Load the stable material; replacing its referenced texture is sufficient."""
    path = f"{CONTENT_ROOT}/M_SFPPlannerScreen.M_SFPPlannerScreen"
    material = unreal.EditorAssetLibrary.load_asset(path)
    if not material:
        raise RuntimeError(
            f"Required packaged screen material is missing: {path}. "
            "Run apply.sh again while the editor is closed."
        )
    return material


def mesh_import_options():
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    # A fresh staging import may create disposable source materials. This makes
    # Unreal preserve every OBJ material group instead of inheriting the older
    # five-slot layout from the stable mesh asset.
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    static_data = options.get_editor_property("static_mesh_import_data")
    static_data.set_editor_property("combine_meshes", True)
    static_data.set_editor_property("generate_lightmap_u_vs", True)
    static_data.set_editor_property("auto_generate_collision", False)
    static_data.set_editor_property("build_nanite", False)
    static_data.set_editor_property("import_uniform_scale", 1.0)
    return options


def disable_nanite(mesh) -> None:
    try:
        settings = mesh.get_editor_property("nanite_settings")
        settings.set_editor_property("enabled", False)
        mesh.set_editor_property("nanite_settings", settings)
    except Exception as error:
        unreal.log_warning(f"Nanite setting was not exposed: {error}")


def rebuild_main_collision(mesh) -> None:
    try:
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        subsystem.remove_collisions(mesh)
        subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
        body_setup = mesh.get_editor_property("body_setup")
        if body_setup:
            body_setup.set_editor_property(
                "collision_trace_flag",
                unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX,
            )
    except Exception as error:
        raise RuntimeError(f"Could not create the required BlockAll collision: {error}") from error


def remove_screen_collision(mesh) -> None:
    try:
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        subsystem.remove_collisions(mesh)
    except Exception as error:
        unreal.log_warning(f"Screen collision could not be removed: {error}")


def slot_name(static_material) -> str:
    for property_name in ("material_slot_name", "imported_material_slot_name"):
        try:
            value = str(static_material.get_editor_property(property_name))
            if value and value != "None":
                return value
        except Exception:
            pass
    return ""


def canonical_slot_name(name: str, expected_slots: tuple[str, ...]) -> str:
    value = name.strip()
    for expected in expected_slots:
        if value == expected or value.startswith(f"{expected}_"):
            return expected
    return value


def read_source_section_slots(source_mesh, expected_slots: tuple[str, ...]) -> list[str]:
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    static_materials = list(source_mesh.get_editor_property("static_materials"))
    section_count = source_mesh.get_num_sections(0)
    if section_count <= 0:
        raise RuntimeError("Fresh staging mesh has no LOD 0 sections")

    section_slots: list[str] = []
    for section_index in range(section_count):
        material_index = subsystem.get_lod_material_slot(source_mesh, 0, section_index)
        if material_index < 0 or material_index >= len(static_materials):
            raise RuntimeError(
                f"Staging section {section_index} references invalid material slot {material_index}"
            )
        raw_name = slot_name(static_materials[material_index])
        canonical = canonical_slot_name(raw_name, expected_slots)
        if canonical not in expected_slots:
            raise RuntimeError(
                f"Unknown staging material slot '{raw_name}' in section {section_index}"
            )
        section_slots.append(canonical)

    missing = [name for name in expected_slots if name not in section_slots]
    if missing:
        visible = ", ".join(slot_name(item) or "<unnamed>" for item in static_materials)
        raise RuntimeError(
            "Fresh staging import did not preserve all material groups. "
            f"Missing: {', '.join(missing)}; imported slots: {visible}"
        )
    return section_slots


def replace_lod_and_material_slots(
    stable_mesh,
    source_mesh,
    expected_slots: tuple[str, ...],
    slot_materials: dict[str, object],
) -> None:
    """Copy fresh geometry and rebuild a deterministic material-slot layout."""
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    section_slots = read_source_section_slots(source_mesh, expected_slots)

    stable_mesh.modify()
    lod_index = subsystem.set_lod_from_static_mesh(stable_mesh, 0, source_mesh, 0, False)
    if lod_index != 0:
        raise RuntimeError(f"Could not replace stable mesh LOD 0; returned {lod_index}")
    if stable_mesh.get_num_sections(0) != len(section_slots):
        raise RuntimeError(
            "LOD transfer changed the section count: "
            f"expected {len(section_slots)}, found {stable_mesh.get_num_sections(0)}"
        )

    rebuilt_slots = [
        unreal.StaticMaterial(
            material_interface=slot_materials[name],
            material_slot_name=unreal.Name(name),
        )
        for name in expected_slots
    ]
    stable_mesh.set_editor_property("static_materials", rebuilt_slots)
    slot_indices = {name: index for index, name in enumerate(expected_slots)}
    for section_index, name in enumerate(section_slots):
        subsystem.set_lod_material_slot(stable_mesh, slot_indices[name], 0, section_index)

    final_slots = list(stable_mesh.get_editor_property("static_materials"))
    if len(final_slots) != len(expected_slots):
        raise RuntimeError(
            f"Material-slot rebuild failed: expected {len(expected_slots)}, found {len(final_slots)}"
        )
    for section_index, name in enumerate(section_slots):
        actual_index = subsystem.get_lod_material_slot(stable_mesh, 0, section_index)
        if actual_index != slot_indices[name]:
            raise RuntimeError(
                f"Section {section_index} mapping failed: expected {slot_indices[name]}, "
                f"found {actual_index}"
            )


def load_required_mesh(path: str):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        raise RuntimeError(f"Required packaged mesh is missing: {path}. Run apply.sh again.")
    return mesh


def main() -> None:
    require_sources()
    unreal.log("Pioneer Production Planner r17 texture-safe terminal setup started")
    textures: dict[str, dict[str, object]] = {}
    for surface in SURFACES:
        textures[surface] = {}
        for kind in ("BaseColor", "Normal", "ORM"):
            name = f"T_PFP_R13_{surface}_{kind}"
            textures[surface][kind] = import_texture(SOURCE / "PBR" / f"{name}.png", name, kind)

    import_texture(SOURCE / "T_SFPPlannerScreen.png", "TEX_T_SFPPlannerScreen", "Screen")
    import_texture(SOURCE / "T_PFP_TerminalIcon.png", "TEX_T_PFP_TerminalIcon", "Icon")

    generated_root = choose_generated_root()
    unreal.log(f"Creating non-destructive r17 staging assets in {generated_root}")
    materials = {
        surface: build_surface_material(surface, textures[surface], generated_root)
        for surface in SURFACES
    }
    glow_material = build_glow_material(generated_root)
    screen_material = get_screen_material()

    main_source_mesh = import_task(
        SOURCE / "SM_SFPPlannerTerminal_r2.obj",
        generated_root,
        "SM_PFP_R17_MainSource",
        mesh_import_options(),
        replace_existing=False,
        replace_existing_settings=True,
    )
    main_mesh = load_required_mesh(MAIN_MESH_PATH)
    main_slot_materials = {
        **{slot: materials[surface] for slot, surface in SLOT_TO_SURFACE.items()},
        "SFP_R2_ScreenGlow": glow_material,
        "SFP_R2_ScreenUI": screen_material,
    }
    replace_lod_and_material_slots(main_mesh, main_source_mesh, SLOT_ORDER, main_slot_materials)
    disable_nanite(main_mesh)
    rebuild_main_collision(main_mesh)
    main_mesh.modify()
    if not unreal.EditorAssetLibrary.save_loaded_asset(main_mesh, False):
        raise RuntimeError("Could not save stable terminal main mesh")
    unreal.log("Saved stable eight-slot terminal main mesh")

    screen_source_mesh = import_task(
        SOURCE / "SM_SFPPlannerTerminalScreen_r3.obj",
        generated_root,
        "SM_PFP_R17_ScreenSource",
        mesh_import_options(),
        replace_existing=False,
        replace_existing_settings=True,
    )
    screen_mesh = load_required_mesh(SCREEN_MESH_PATH)
    replace_lod_and_material_slots(
        screen_mesh,
        screen_source_mesh,
        ("SFP_R3_ScreenUI",),
        {"SFP_R3_ScreenUI": screen_material},
    )
    disable_nanite(screen_mesh)
    remove_screen_collision(screen_mesh)
    screen_mesh.modify()
    if not unreal.EditorAssetLibrary.save_loaded_asset(screen_mesh, False):
        raise RuntimeError("Could not save stable terminal screen mesh")
    unreal.log("Saved stable terminal screen mesh")

    unreal.log_warning(
        "r17 texture-safe setup finished. Open Build_SFPPlannerTerminal, Compile, Save, then Save All."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        unreal.log_error(f"Pioneer Production Planner r17 setup failed: {error}")
        raise
