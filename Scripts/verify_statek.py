"""PIE measurements, five rendered showroom states and a rail-clearance debug view.

Run UnrealEditor HorrorProject.uproject /Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase
    -ExecutePythonScript=<absolute path to this script> -RenderOffscreen -unattended -NoSound
Add -StatekBaseline on the pre-integration revision to record the collision fingerprint and
rehearse camera positions. Baseline images and all raw PNGs stay under Saved/StatekProof.
"""
import hashlib
import json
import math
import time
import traceback
from pathlib import Path

import unreal as ue


ROOT = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
WORK = ROOT / "Saved/StatekProof"
DOC = ROOT / "Docs/StudentShowcase/StatekFPP"
MAP = "/Game/StudentShowcase/Maps/L_ModelsAndMechanics_Showcase"
BASELINE = "-statekbaseline" in ue.SystemLibrary.get_command_line().lower()
GAME = ue.GameplayStatics
V = ue.Vector
NO_COLLISION = ue.CollisionEnabled.NO_COLLISION
WORK.mkdir(parents=True, exist_ok=True)
editor = ue.get_editor_subsystem(ue.UnrealEditorSubsystem)
levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)


def p(obj, name):
    return obj.get_editor_property(name)


def vec(v):
    return [round(float(x) + 0., 3) or 0. for x in (v.x, v.y, v.z)]


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def collision_fingerprint(world):
    rows = {}
    for actor in GAME.get_all_actors_with_tag(world, "ShipLightModuleShowroom"):
        tag = next(str(t) for t in actor.tags if str(t).startswith("LAB-MODULE-"))
        for c in actor.get_components_by_class(ue.PrimitiveComponent):
            if not actor.get_actor_enable_collision() or c.get_collision_enabled() == NO_COLLISION:
                continue
            body = p(c, "body_instance")
            rot = c.get_world_rotation()
            center, extent, _ = ue.SystemLibrary.get_component_bounds(c)
            mesh = c.static_mesh if isinstance(c, ue.StaticMeshComponent) else None
            rows[tag + "/" + c.get_name()] = {
                "class": c.get_class().get_name(), "mesh": mesh.get_path_name() if mesh else None,
                "center_cm": vec(center), "extent_cm": vec(extent), "scale": vec(c.get_world_scale()),
                "pitch_yaw_roll": [round(n, 2) or 0. for n in (rot.pitch, rot.yaw, rot.roll)],
                "profile": str(c.get_collision_profile_name()), "mode": c.get_collision_enabled().name,
                "type": c.get_collision_object_type().name,
                "responses": [c.get_collision_response_to_channel(ch).name for ch in (
                    ue.CollisionChannel.ECC_PAWN, ue.CollisionChannel.ECC_VISIBILITY,
                    ue.CollisionChannel.ECC_PHYSICS_BODY, ue.CollisionChannel.ECC_WORLD_STATIC,
                    ue.CollisionChannel.ECC_WORLD_DYNAMIC, ue.CollisionChannel.ECC_CAMERA)],
                "simulates": p(body, "simulate_physics"), "mass_override_kg": round(p(body, "mass_in_kg_override"), 4),
                "overrides_mass": p(body, "override_mass"), "gravity": p(body, "enable_gravity"),
                "linear_damping": round(c.get_linear_damping(), 3), "angular_damping": round(c.get_angular_damping(), 3),
            }
    return rows


def fingerprint(rows):
    return hashlib.sha256(json.dumps(rows, sort_keys=True).encode()).hexdigest()


def baseline_survey():
    assert levels.load_level(MAP)
    rows = collision_fingerprint(editor.get_editor_world())
    write(WORK / "baseline-collision.json", rows)
    ue.log("STATEK original collision hash " + fingerprint(rows))


def one(world, tag):
    actors = GAME.get_all_actors_with_tag(world, tag)
    assert len(actors) == 1, (tag, len(actors))
    return actors[0]


def part(actor, name):
    return next(c for c in actor.get_components_by_class(ue.SceneComponent) if c.get_name() == name)


def geometry(mesh):
    # Read the indexed triangles Unreal actually built at LOD 0, in mesh-local cm.
    vertices, triangles = [], []
    for section in range(mesh.get_num_sections(0)):
        points, indices, *_ = ue.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, section)
        vertices.extend(points)
        for i in range(0, len(indices), 3):
            a, b, c = (points[indices[j]] for j in (i,i+1,i+2))
            n = (b-a).cross(c-a)
            area2 = n.length()
            if area2 > .001:
                triangles.append(((a+b+c)/3., n/area2, area2/2.))
    return vertices, triangles


def limits(vertices):
    coords = list(zip(*(vec(v) for v in vertices)))
    return [[min(axis) for axis in coords], [max(axis) for axis in coords]]


def shape(actor, mesh_name):
    pairs = [c for c in actor.get_components_by_class(ue.StaticMeshComponent)
             if c.static_mesh and c.static_mesh.get_name() == mesh_name]
    assert len(pairs) == 1, (actor.get_name(), mesh_name, pairs)
    return pairs[0]


def fitted_art(world, result):
    """Measure the installed meshes, physical components and their assembly before moving anything."""
    module = lambda name: one(world, "LAB-MODULE-" + name)
    walls = [module("WALL-" + side) for side in "ABC"]
    floor, guide = module("FLOOR"), module("GUIDE")
    measurements = {}
    meshes = {}
    for short in ("PanelWall_Face", "PanelWall_Frame", "PanelFloor_Face", "PanelFloor_Frame",
                  "Guide_Rotor", "Guide_Mount", "Guide_Pocket", "CargoCrate", "ServiceBall"):
        name = "SM_" + short
        mesh = ue.load_asset("/Game/StatekFPP/Meshes/" + name)
        points, triangles = geometry(mesh)
        meshes[short] = (points, triangles)
        lo, hi = limits(points)
        measurements[short] = {"min_xyz_cm": lo, "max_xyz_cm": hi,
                               "size_xyz_cm": [round(b-a, 3) for a,b in zip(lo, hi)]}
    result["local_lod0_mesh_bounds"] = measurements
    for name, dims in {"PanelWall_Face": [26,132,134], "PanelFloor_Face": [10,156,156],
                       "CargoCrate": [72,72,72], "ServiceBall": [38,38,38]}.items():
        assert all(abs(a-b)<.15 for a,b in zip(dims, measurements[name]["size_xyz_cm"])), name

    vertices, faces = meshes["Guide_Rotor"]
    slab_lo, slab_hi = limits([v for v in vertices if abs(v.x) <= 8.001])
    slab = [round(b-a, 3) for a,b in zip(slab_lo,slab_hi)]
    assert all(abs(a-b)<.1 for a,b in zip(slab,(16,220,106))), slab
    rail_planes = sorted(set(round(c.z, 2) for c,n,area in faces
                             if abs(n.z) > .999 and c.x > 9 and area > 750))
    free = min(v for v in rail_planes if v>0) - max(v for v in rail_planes if v<0)
    assert abs(free-46) < .1, rail_planes
    visual_edges = []
    for transverse in (-free/2,free/2):
        # On the inside wall plane, identify its upper edge along the constant 15° grade.
        inner = [v for v in vertices if abs(v.z-transverse)<.025 and v.x>20]
        intercept = lambda v: v.x + v.y*math.tan(math.radians(15))
        highest = max(intercept(v) for v in inner)
        edge = [v for v in inner if abs(intercept(v)-highest)<.12]
        ends = sorted(edge,key=lambda v:v.y)
        assert ends[-1].y-ends[0].y>200, (transverse, highest, ends)
        visual_edges.append([vec(ends[0]),vec(ends[-1])])
    physical_rails = [part(guide, n) for n in ("LeftRail", "RightRail")]
    rail_gap = ((physical_rails[0].get_world_location()-physical_rails[1].get_world_location()).length()
                - sum(abs(c.get_world_scale().z)*50 for c in physical_rails))
    ramp = part(guide, "Ramp")
    max_sweep = max(math.hypot(v.y,v.z) for v in vertices)
    # Top plane at the center line. Its projected slope is in the local XY plane.
    top_faces = [(c,n) for c,n,area in faces if area>400 and c.x>9 and abs(abs(n.x)-math.cos(math.radians(15))) < .004]
    floor_intercept = max(c.x + c.y*n.y/n.x for c,n in top_faces)
    physical_intercept = ramp.get_world_location().z-guide.get_actor_location().z + 2.5/ramp.get_forward_vector().z
    result["guide_cm"] = {
        "base_xyz": slab, "rail_inner_and_outer_planes": rail_planes, "visual_rail_gap": free,
        "visual_inner_edges_local":visual_edges,
        "physics_rail_gap": round(rail_gap,3), "gap_difference": round(rail_gap-free,3),
        "ball_clearance_total": round(free-measurements["ServiceBall"]["size_xyz_cm"][0],3),
        "rotation_radius": round(max_sweep,3), "visual_ramp_center_height": round(floor_intercept,3),
        "physics_ramp_center_height": round(physical_intercept,3),
        "mount_bottom_world_z": round(guide.get_actor_location().z+measurements["Guide_Mount"]["min_xyz_cm"][0],3),
    }
    deck = next(a for a in ue.get_editor_subsystem(ue.EditorActorSubsystem).get_all_level_actors()
                if a.get_actor_label() == "Poklad bezpieczny")
    dc,dh,_ = ue.SystemLibrary.get_component_bounds(deck.static_mesh_component)
    result["deck_z_cm"] = dc.z+dh.z
    result["folded_wall"] = {
        "spacing_cm": [(a.get_actor_location()-b.get_actor_location()).length() for a,b in zip(walls,walls[1:])],
        "face_front_local_x": measurements["PanelWall_Face"]["max_xyz_cm"][0],
        "frame_furthest_detail_local_x": measurements["PanelWall_Frame"]["max_xyz_cm"][0],
        "collider_front_local_x": p(walls[1], "BlockSize").x/2,
        "folded_floor_surface_z": floor.get_actor_location().z+measurements["PanelFloor_Face"]["max_xyz_cm"][0],
    }
    pocket_verts = meshes["Guide_Pocket"][0]
    # Mesh's broad floor is 6 cm above its pivot. Include the inset rubber mat separately.
    floor_mat = max(v.x for v in pocket_verts if -36.1<v.y<36.1 and abs(v.z)<46)
    sill_height = max(v.x for v in pocket_verts if v.y < -40)
    stop_height = max(v.x for v in pocket_verts if v.y > 39)
    result["pockets_cm"] = {}
    for letter, cardinal in zip("ABCD", "NWSE"):
        vis = part(guide, "StatekPocket" + letter)
        radial = vis.get_right_vector()
        pivot = vis.get_world_location()
        delta = pivot - guide.get_actor_location()
        physic = {}
        for key in ("BASE", "SILL", "STOP","LEFT","RIGHT"):
            comp = module("POCKET-" + cardinal + "-" + key).static_mesh_component
            center, half, _ = ue.SystemLibrary.get_component_bounds(comp)
            d = (center-guide.get_actor_location()).dot(radial)
            e = abs(half.x*radial.x)+abs(half.y*radial.y)
            physic[key] = {"radial_bounds": [round(d-e,2),round(d+e,2)],"top_z":round(center.z+half.z,2)}
        radial_center = delta.dot(radial)
        data = {"visual_pivot_world":vec(pivot), "visual_outward":vec(radial), "projection_from_rotor":round(radial_center,3),
                "visual_base_radial_bounds":[round(radial_center+measurements["Guide_Pocket"]["min_xyz_cm"][1],3),
                                             round(radial_center+measurements["Guide_Pocket"]["max_xyz_cm"][1],3)],
                "visual_mat_top_z": round(pivot.z+floor_mat,3), "visual_sill_top_z": round(pivot.z+sill_height,3),
                "visual_stop_highest_z": round(pivot.z+stop_height,3), "physics":physic}
        data["visual_minus_physics_cm"] = {
            "floor_mat_top":round(data["visual_mat_top_z"]-physic["BASE"]["top_z"],3),
            "sill_top":round(data["visual_sill_top_z"]-physic["SILL"]["top_z"],3),
            "stop_highest":round(data["visual_stop_highest_z"]-physic["STOP"]["top_z"],3),
            "floor_radial_center":round(radial_center-sum(physic["BASE"]["radial_bounds"])/2,3),
        }
        data["pocket_to_sweep_clearance"] = round(data["visual_base_radial_bounds"][0]-max_sweep,3)
        result["pockets_cm"][letter] = data
        assert vis.get_collision_enabled()==NO_COLLISION and abs(radial_center-175)<.1 and abs(delta.z-4.14)<.1
    checks = {}
    for actor in walls+[floor,guide,module("CRATE-A"),module("CRATE-B"),module("BALL")]:
        for c in actor.get_components_by_class(ue.StaticMeshComponent):
            if c.static_mesh and c.static_mesh.get_path_name().startswith("/Game/StatekFPP/"):
                key = actor.get_name()+"/"+c.get_name()
                checks[key] = {"mesh":c.static_mesh.get_name(),"collision":c.get_collision_enabled().name,
                               "visible":c.is_visible() and not p(c,"hidden_in_game"),
                               "scale":vec(c.get_world_scale())}
    result["visual_components"] = checks
    assert len(checks)==17 and all(c["collision"]=="NO_COLLISION" and c["visible"] for c in checks.values()), checks


def suite(result):
    assert editor.get_editor_world().get_path_name().split(".")[0] == MAP, "Open the showroom map first"
    rows = collision_fingerprint(editor.get_editor_world())
    current = fingerprint(rows)
    result["collision_settings"] = {"original_sha256": "01377f918234a0418be86c951fa8e6ab2d1a485930d10b742699d583d8d2a5cf",
                                    "current_sha256":current, "active_components":len(rows)}
    if BASELINE:
        write(WORK / "baseline-collision.json", rows)
    elif current != result["collision_settings"]["original_sha256"]:
        original = WORK / "baseline-collision.json"
        if original.exists():
            old = json.loads(original.read_text(encoding="utf8"))
            result["collision_settings"]["changed"] = {
                k:{"original":old.get(k),"current":rows.get(k)} for k in set(old)|set(rows) if old.get(k)!=rows.get(k)}
        raise AssertionError("Original collision, transform or physics settings changed")
    if not BASELINE:
        fitted_art(editor.get_editor_world(), result)
    for command in ("t.IdleWhenNotForeground 0", "Slate.bAllowThrottling 0", "t.MaxFPS 45"):
        ue.SystemLibrary.execute_console_command(editor.get_editor_world(), command)
    levels.editor_request_begin_play()
    while not ue.EditorLevelLibrary.get_pie_worlds(False):
        yield
    world = ue.EditorLevelLibrary.get_pie_worlds(False)[0]
    while not GAME.get_player_pawn(world,0):
        yield
    pc = GAME.get_player_controller(world,0)
    pawn = GAME.get_player_pawn(world,0)
    cam = pawn.get_component_by_class(ue.CameraComponent)
    normal_eye = p(cam, "relative_location")
    kit = GAME.get_actor_of_class(world,ue.ShipSurvivor)
    home_z = pawn.get_actor_location().z
    module = lambda name: one(world, "LAB-MODULE-" + name)
    walls = [module("WALL-" + letter) for letter in "ABC"]
    middle, floor, guide = walls[1], module("FLOOR"), module("GUIDE")
    ball = module("BALL")
    sphere = ball.static_mesh_component
    block = lambda a: p(a, "Block")
    moving = lambda a: block(a) if BASELINE else part(a, "MovingVisual")
    fixed = lambda a: a.root_component if BASELINE else part(a, "FixedVisual")
    start = {a: moving(a).get_world_location() for a in (middle,floor)}
    fixed_origins = {a:fixed(a).get_world_location() for a in (middle,floor,guide)}
    result["world"] = world.get_path_name()
    result["engine"] = ue.SystemLibrary.get_engine_version()

    def wait(seconds):
        until = GAME.get_time_seconds(world)+seconds
        while GAME.get_time_seconds(world)<until:
            yield

    def tap(key):
        ue.SystemLibrary.execute_console_command(world,"Input.+key "+key,pc)
        yield
        ue.SystemLibrary.execute_console_command(world,"Input.-key "+key,pc)
        yield from wait(.15)

    def frame(actor, dx,dy,aim_z, fov, target_offset=V(0,0,0), elevation=0):
        center = actor.get_actor_location()
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(V(center.x+dx,center.y+dy,home_z),False,True)
        cam.field_of_view = fov
        cam.set_relative_location(normal_eye+V(0,0,elevation),False,True)
        yield from wait(.35)
        focus = V(center.x,center.y,aim_z) + target_offset
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(cam.get_world_location(),focus))
        yield from wait(.45)

    def shot(label, state, camera=None):
        dest = WORK if BASELINE else DOC
        dest.mkdir(parents=True,exist_ok=True)
        stamp = time.time_ns()
        png = WORK / (label+"-"+str(stamp)+".png")
        ue.SystemLibrary.execute_console_command(world,f'HighResShot 1400x875 filename="{png.as_posix()}"',pc)
        # Wait for an actual, complete PNG from this new request.
        while True:
            if png.exists() and png.stat().st_size>32:
                with png.open("rb") as f:
                    f.seek(-8,2)
                    if f.read(4)==b"IEND":
                        break
            yield
        jpg = dest / (label+".jpg")
        tex = ue.RenderingLibrary.import_file_as_texture2d(world, str(png))
        for _ in range(5):
            yield
        opts = ue.ImageWriteOptions()
        opts.set_editor_property("async_",False)
        opts.set_editor_property("format",ue.DesiredImageFormat.JPG)
        opts.set_editor_property("compression_quality",91)
        ue.ImageWriteBlueprintLibrary.export_to_disk(tex,str(jpg),opts)
        assert jpg.is_file() and jpg.stat().st_size>12000, jpg
        result["shots"][label] = {"file":jpg.name,"sha256":hashlib.sha256(jpg.read_bytes()).hexdigest(),
                                 "camera_world_cm":vec((camera or cam).get_world_location()),"state":state}
        write(WORK / "progress.json",result)

    def lamp(enabled):
        if bool(p(kit,"bLampOn")) != enabled:
            yield from tap("F")
        assert bool(p(kit,"bLampOn")) == enabled, p(kit,"Equipped")

    def select_filter(name):
        yield from lamp(False)
        for _ in range(6):
            if str(p(kit,"Filter")) == name:
                break
            yield from tap("Q")
        assert str(p(kit,"Filter"))==name

    def delta(actor, travel):
        move = moving(actor).get_world_location()-start[actor]
        physics = block(actor).get_world_location()-start[actor]
        normal = actor.get_actor_forward_vector()
        assert (move-normal*travel).length()<.2 and (move-physics).length()<.2
        assert (fixed(actor).get_world_location()-fixed_origins[actor]).length()<.01
        data = {"moving_visual_delta_cm":vec(move),"block_delta_cm":vec(physics),
                "fixed_visual_delta_cm":vec(fixed(actor).get_world_location()-fixed_origins[actor])}
        if not BASELINE:
            component = moving(actor)
            i = component.get_material_index("Indicator")
            material = component.get_material(i)
            assert i>=0 and isinstance(material,ue.MaterialInstanceDynamic)
            color = material.get_vector_parameter_value("IndicatorColor")
            data["indicator_linear_rgb_while_illuminated"] = [round(n,3) for n in (color.r,color.g,color.b)]
            data["intensity_while_illuminated"] = material.get_scalar_parameter_value("Intensity")
        return data

    yield from wait(1.)
    yield from frame(middle, -140, 460, 97, 64)
    retracted = [round(float(p(w,"Extension")),2) for w in walls]
    assert retracted==[0,0,0]
    yield from shot("01-panele-schowane", {"extensions_cm":retracted})
    pickup = next(a for a in GAME.get_all_actors_of_class(world,ue.ShipFixture) if str(p(a,"Kind"))=="ColorLamp")
    pickup.use(kit)
    yield from wait(.3)
    inv = next(c for c in pawn.get_components_by_class(ue.ActorComponent) if c.get_class().get_name()=="BPC_Inventory_C")
    for key, row in zip("One Two Three Four Five Six Seven Eight".split(),p(inv,"InventorySlots")):
        item = p(row,"Item_3_140BEB7D45592192CDA69DB2EE378C9B")
        if item and item.get_name()=="BP_Pack_ColorLamp_C":
            yield from tap(key)
            break
    assert str(p(kit,"Equipped"))=="ColorLamp"
    yield from select_filter("Red")
    while p(kit,"RadiusLevel") > 1:
        yield from tap("C")
    yield from frame(middle,-75,266,80,83)
    yield from lamp(True)
    yield from wait(1.05)
    extension_values = [round(p(w,"Extension")) for w in walls]
    assert extension_values == [0,92,0], extension_values
    state = delta(middle,92)
    yield from shot("02-panel-wysuniety",state)
    yield from lamp(False)

    yield from frame(floor,161,70,-4,62)
    yield from lamp(True)
    yield from wait(1.1)
    data = delta(floor,100)
    yield from lamp(False)
    yield from frame(floor,211,256,46,53,elevation=62)
    yield from shot("03-podloga-podniesiona",data)

    yield from select_filter("UV")
    yield from frame(guide,225,-200,64,62)
    initial = moving(guide).get_right_vector()
    yield from lamp(True)
    yield from wait(.92)
    after = moving(guide).get_right_vector()
    assert p(guide,"QuarterTurns")==1 and abs(initial.dot(after))<.002
    assert (fixed(guide).get_world_location()-fixed_origins[guide]).length()<.01
    assert after.dot(block(guide).get_right_vector())>.999
    pocket = module("POCKET-W-BASE").static_mesh_component
    pp = pocket.get_world_location()-guide.get_actor_location()
    assert after.dot(pp)/math.hypot(pp.x,pp.y)>.999
    yield from lamp(False)
    yield from frame(guide,240,360,65,65,V(-20,0,0),elevation=300)
    data = {"turns":p(guide,"QuarterTurns"),"outlet_before":vec(initial),"outlet_after":vec(after),
            "physical_outlet":vec(block(guide).get_right_vector()),"pocket":"B",
            "angle_deg":round(math.degrees(math.acos(max(-1.,min(1.,initial.dot(after))))),3)}
    yield from shot("04-prowadnica-po-obrocie", data)

    yield from frame(guide,-28,220,102,52,elevation=157)
    ramp = part(guide,"Ramp")
    up = ramp.get_forward_vector()
    along = ramp.get_right_vector()
    center = ramp.get_world_location()+up*2.5
    radius = 19.
    # Stage one release above the centerline. Keep the existing body simulating.
    ball.set_actor_location(center-along*38+up*(radius+15),False,True)
    sphere.set_physics_linear_velocity(V())
    sphere.set_physics_angular_velocity_in_degrees(V())
    sphere.wake_all_rigid_bodies()
    timeout = GAME.get_time_seconds(world)+2
    while GAME.get_time_seconds(world)<timeout:
        pos = sphere.get_world_location()
        separation = (pos-center).dot(up)-radius
        along_track = (pos-center).dot(along)
        speed = sphere.get_physics_linear_velocity().dot(along)
        if abs(separation)<1.6 and abs(along_track)<75 and speed>3:
            break
        yield
    else:
        raise AssertionError(("Ball did not reach slope in motion", vec(pos), separation, along_track, speed))
    GAME.set_game_paused(world,True)
    try:
        data = {"center_world_cm":vec(pos),"radius_cm":radius,"distance_from_ramp_surface_minus_radius_cm":round(separation,3),
                "cross_track_cm":round((pos-center).dot(ramp.get_up_vector()),3),"along_track_cm":round(along_track,3),
                "downhill_speed_cm_s":round(speed,3),"simulating":sphere.is_simulating_physics()}
        if not BASELINE:
            shell = shape(ball,"SM_ServiceBall")
            data["visual_center_error_cm"] = round((shell.get_world_location()-pos).length(),3)
            assert data["visual_center_error_cm"] < .1
        yield from shot("05-kula-w-torze",data)
    finally:
        GAME.set_game_paused(world,False)
    cp,extent,_ = ue.SystemLibrary.get_component_bounds(pocket)
    top = cp.z+extent.z
    deadline = GAME.get_time_seconds(world)+18
    stable = None
    while GAME.get_time_seconds(world)<deadline:
        final = sphere.get_world_location()
        speed = sphere.get_physics_linear_velocity().length()
        if abs(final.x-cp.x)<extent.x and abs(final.y-cp.y)<extent.y and abs(final.z-radius-top)<2 and speed<6:
            if stable is None:
                stable = GAME.get_time_seconds(world)
            if GAME.get_time_seconds(world)-stable>.65:
                break
        else:
            stable = None
        yield
    else:
        raise AssertionError(("Ball did not come to rest in physical pocket B",vec(final),top,speed))
    result["physical_roll_to_B"] = {"ball_center":vec(final),"floor_z":top,"speed_cm_s":round(speed,3)}
    assert len(result["shots"])==5
    if BASELINE:
        return

    # This transient PIE camera and diagnostic lines exist only after the five ordinary views.
    pose = ue.Transform()
    pose.translation = guide.get_actor_location()+V(0,0,435)
    pose.rotation = ue.Rotator(pitch=-90,yaw=90,roll=0).quaternion()
    statics = ue.get_default_object(ue.GameplayStatics)
    scale = ue.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT
    overhead = statics.call_method("BeginDeferredActorSpawnFromClass",
        (world,ue.CameraActor,pose,ue.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,scale))
    overhead = statics.call_method("FinishSpawningActor",(overhead,pose,scale))
    lens = overhead.get_component_by_class(ue.CameraComponent)
    lens.field_of_view=51
    pc.set_view_target_with_blend(overhead,0)
    yield from wait(.5)
    debug = {"view":"PIE, top down","line_vertical_lift_cm":.8,"line_thickness_cm":1.25}

    def mark(description, color, gap, pairs):
        debug[description] = {"gap_cm":gap,"linear_rgb":[color.r,color.g,color.b],
                              "world_edge_endpoints_cm":[[vec(a),vec(b)] for a,b in pairs]}
        offset = V(0,0,debug["line_vertical_lift_cm"])
        for a,b in pairs:
            ue.SystemLibrary.draw_debug_line(world,a+offset,b+offset,color,25,debug["line_thickness_cm"])

    edges = result["guide_cm"]["visual_inner_edges_local"]
    visual_t = moving(guide).get_world_transform()
    mark("cyan_rendered_inner_rail_edges",ue.LinearColor(0,1,1,1),result["guide_cm"]["visual_rail_gap"],
         [(visual_t.transform_location(V(*a)),visual_t.transform_location(V(*b))) for a,b in edges])
    physics_edges = []
    for name,inner_side in (("LeftRail",-50),("RightRail",50)):
        t = part(guide,name).get_world_transform()
        physics_edges.append((t.transform_location(V(50,-50,inner_side)),t.transform_location(V(50,50,inner_side))))
    mark("orange_physics_inner_rail_edges",ue.LinearColor(1,.16,0,1),result["guide_cm"]["physics_rail_gap"],physics_edges)
    yield from wait(.15)
    yield from shot("06-przeswit-burt",debug,lens)
    pc.set_view_target_with_blend(pawn,0)

    yield from frame(middle,0,250,78,60)
    receiver = moving(middle)
    slot = receiver.get_material_index("Indicator")
    readings = {}
    result["live_filter_indicator"] = readings
    for name, expected in (("Red",(1,.06,.04)),("Green",(.1,1,.18)),("White",(1,1,1)),
                           ("UV",(.55,.1,1)),("Blue",(.08,.35,1))):
        yield from select_filter(name)
        yield from lamp(True)
        yield from wait(.35)
        mat = receiver.get_material(slot)
        color = mat.get_vector_parameter_value("IndicatorColor")
        rgb = (color.r,color.g,color.b)
        readings[name] = {"rgb":[round(n,3) for n in rgb],"intensity":mat.get_scalar_parameter_value("Intensity")}
        assert all(abs(a-b)<.002 for a,b in zip(rgb,expected)), (name,rgb)
    yield from lamp(False)
    yield from wait(.45)
    mat = receiver.get_material(slot)
    idle = mat.get_vector_parameter_value("IndicatorColor")
    readings["lamp_off"] = {"rgb":[round(n,3) for n in (idle.r,idle.g,idle.b)],
                            "intensity":mat.get_scalar_parameter_value("Intensity"),"lamp_on":bool(p(kit,"bLampOn"))}
    assert readings["lamp_off"]["rgb"] == [.15,.7,.74] and not readings["lamp_off"]["lamp_on"]


def start_test():
    result = {"status":"RUNNING","baseline":BASELINE,"shots":{}}
    write(WORK/"progress.json",result)
    sequence = suite(result)
    started = time.monotonic()
    running = False
    ue.EditorPythonScripting.set_keep_python_script_alive(True)

    def tick(_dt):
        nonlocal running
        if running:
            return
        running = True
        try:
            if time.monotonic()-started>255:
                raise TimeoutError("PIE verification exceeded 255 seconds")
            next(sequence)
            return
        except StopIteration:
            result["status"]="PASS"
        except Exception:
            result["status"]="FAIL"
            result["exception"]=traceback.format_exc()
            ue.log_error(result["exception"])
        finally:
            running=False
        write(WORK/"progress.json",result)
        if result["status"]=="PASS" and not BASELINE:
            write(DOC/"pomiary-pie.json",result)
        ue.unregister_slate_post_tick_callback(handle)
        if ue.EditorLevelLibrary.get_pie_worlds(False):
            levels.editor_request_end_play()
        ue.EditorPythonScripting.set_keep_python_script_alive(False)

    handle = ue.register_slate_post_tick_callback(tick)


if "-stateksurvey" in ue.SystemLibrary.get_command_line().lower():
    baseline_survey()
else:
    start_test()
