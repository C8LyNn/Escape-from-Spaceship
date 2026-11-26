"""Install the imported render meshes on the existing showroom actors. Run in the Unreal editor.

Requires the StatekFPP assets from import_statek.py and the rebuilt HorrorProject module.
Only the current showroom map is saved. The original collision components stay on their actors.
"""
import unreal as ue


MAP = "/Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase"
MESHES = "/Game/StatekFPP/Meshes/"
PREFIX = "LAB-MODULE-"


def add_visual(owner, name, tag, mesh):
    """Create or reuse a serialized component owned by this instance, under its original root."""
    components = owner.get_components_by_class(ue.StaticMeshComponent)
    existing = [comp for comp in components if comp.get_name() == name]
    if existing:
        component, = existing
        if not component.component_has_tag(tag):
            raise RuntimeError(f"{owner.get_actor_label()}.{name} is already in use")
    else:
        subsystem = ue.get_engine_subsystem(ue.SubobjectDataSubsystem)
        library = ue.SubobjectDataBlueprintFunctionLibrary
        def object_for(handle):
            return library.get_associated_object(library.get_data(handle))
        handles = subsystem.k2_gather_subobject_data_for_instance(owner)
        root_handle, = [handle for handle in handles if object_for(handle) == owner.root_component]
        params = ue.AddNewSubobjectParams(parent_handle=root_handle, new_class=ue.StaticMeshComponent)
        handle, reason = subsystem.add_new_subobject(params)
        if not object_for(handle) or not subsystem.rename_subobject(handle, name):
            raise RuntimeError(f"Could not create {name} on {owner.get_actor_label()}: {reason}")
        component = object_for(handle)
        component.set_editor_property("component_tags", [tag])
    component.modify()
    component.set_mobility(ue.ComponentMobility.MOVABLE)
    component.set_collision_profile_name("NoCollision")
    component.set_editor_property("generate_overlap_events", False)
    component.set_editor_property("can_ever_affect_navigation", False)
    component.set_absolute(False, False, True)
    component.set_world_scale3d(ue.Vector(1, 1, 1))
    component.set_static_mesh(mesh)
    assert component.get_attach_parent() == owner.root_component
    assert component.get_collision_enabled() == ue.CollisionEnabled.NO_COLLISION
    return component


def install():
    editor = ue.get_editor_subsystem(ue.UnrealEditorSubsystem)
    if editor.get_editor_world().get_path_name().split(".")[0] != MAP:
        raise RuntimeError("Open L_ModelsAndMechanics_Showcase before running this script.")
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem).get_all_level_actors()
    tagged = {str(tag): actor for actor in actors for tag in actor.tags if str(tag).startswith(PREFIX)}

    def station(name):
        return tagged[PREFIX + name]

    names = ("PanelWall_Face", "PanelWall_Frame", "PanelFloor_Face", "PanelFloor_Frame",
             "Guide_Rotor", "Guide_Mount", "Guide_Pocket", "CargoCrate", "ServiceBall")
    meshes = {name: ue.load_asset(MESHES + "SM_" + name) for name in names}
    if not all(isinstance(mesh, ue.StaticMesh) for mesh in meshes.values()):
        raise RuntimeError("Import all nine StatekFPP meshes first.")
    directions = (("A", "N"), ("B", "W"), ("C", "S"), ("D", "E"))
    guide = station("GUIDE")
    for suffix, moving, fixed in (
        ("WALL-A", "PanelWall_Face", "PanelWall_Frame"),
        ("WALL-B", "PanelWall_Face", "PanelWall_Frame"),
        ("WALL-C", "PanelWall_Face", "PanelWall_Frame"),
        ("FLOOR", "PanelFloor_Face", "PanelFloor_Frame"),
        ("GUIDE", "Guide_Rotor", "Guide_Mount"),
    ):
        actor = station(suffix)
        actor.modify()
        actor.set_editor_property("moving_visual_mesh", meshes[moving])
        actor.set_editor_property("fixed_visual_mesh", meshes[fixed])

    for suffix, mesh in (("CRATE-A", "CargoCrate"), ("CRATE-B", "CargoCrate"), ("BALL", "ServiceBall")):
        actor = station(suffix)
        actor.modify()
        shell = add_visual(actor, "StatekShell", "STATEK-VIS-" + suffix, meshes[mesh])
        shell.set_relative_location(ue.Vector(), False, False)
        shell.set_relative_rotation(ue.Rotator(), False, False)
        actor.root_component.modify()
        actor.root_component.set_visibility(False, False)

    for quarter, (letter, direction) in enumerate(directions):
        pocket = add_visual(guide, "StatekPocket" + letter, "STATEK-VIS-POCKET-" + letter, meshes["Guide_Pocket"])
        # UE Roll has the opposite sign to an axis-angle rotation about positive local X.
        rotation = ue.Rotator(roll=-90 * quarter)
        local_origin = ue.MathLibrary.greater_greater_vector_rotator(ue.Vector(4.14, 175, 0), rotation)
        pocket.set_relative_location(local_origin, False, False)
        pocket.set_relative_rotation(rotation, False, False)
        for part in ("BASE", "SILL", "LEFT", "RIGHT", "STOP"):
            proxy = station("POCKET-" + direction + "-" + part).root_component
            proxy.modify()
            proxy.set_visibility(False, False)
        # The old letters face away from the track. Reuse them on the inside of the new backstops.
        label = station("POCKET-" + direction + "-LABEL")
        label.modify()
        plaque_center = pocket.get_world_transform().transform_location(ue.Vector(38, 41.7, 0))
        facing = ue.MathLibrary.make_rot_from_x(pocket.get_right_vector() * -1)
        label.set_actor_location_and_rotation(plaque_center, facing, False, False)
        text = label.get_component_by_class(ue.TextRenderComponent)
        text.modify()
        text.set_world_size(10)
        assert str(text.get_editor_property("text")) == letter

    if not ue.get_editor_subsystem(ue.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Could not save the showroom.")
    ue.log("STATEK_SCENE_SAVED")


if __name__ == "__main__":
    install()
