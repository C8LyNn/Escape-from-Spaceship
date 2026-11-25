"""Exercise the installed showroom wall, platform and ball guide in real-time PIE.

Call cases(world, check, scenario) in a fresh game world for each scenario.
Only starting positions, the sight-line obstruction, and ball resets are staged.
The real player, hand inventory, flashlight, input bindings and Chaos do the work.
"""
import math

import unreal as ue


SCENARIOS = ('red', 'cargo', 'blue', 'guide')
V = ue.Vector
ZERO = V(0., 0., 0.)
UP = V(0., 0., 1.)


def xyz(v):
    return [round(float(n), 2) for n in (v.x, v.y, v.z)]


def dot(a, b):
    return a.x * b.x + a.y * b.y + a.z * b.z


def bounds(component):
    return ue.SystemLibrary.get_component_bounds(component)[:2]


def prop(actor, name):
    return actor.get_editor_property(name)


def face(panel):
    block = prop(panel, 'Block')
    center = block.get_world_location()
    return center + block.get_forward_vector() * (prop(panel, 'BlockSize').x * .5)


def reset_body(actor, mesh, center):
    """Center an upright body at a repeatable release point, initially at rest."""
    actor.set_actor_rotation(ue.Rotator(roll=0., pitch=0., yaw=0.), True)
    actor.set_actor_location(actor.get_actor_location() + center - bounds(mesh)[0], False, True)
    if mesh.is_simulating_physics():
        mesh.set_physics_linear_velocity(ZERO)
        mesh.set_physics_angular_velocity_in_degrees(ZERO)
        mesh.wake_all_rigid_bodies()


def cases(world, check, scenario):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    stock_lamp = pawn.get_component_by_class(ue.SpotLightComponent)
    beam = kit.get_component_by_class(ue.SpotLightComponent)
    battery = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                   if c.get_class().get_name() == 'BPC_Flashlight_C')
    home_z = pawn.get_actor_location().z
    fixtures = game.get_all_actors_of_class(world, ue.ShipFixture)

    def require(label, condition, evidence=None):
        check(label, bool(condition), evidence)
        if not condition:
            raise AssertionError(f'{label}: {evidence}')

    def tagged(tag):
        matches = game.get_all_actors_with_tag(world, tag)
        if len(matches) != 1:
            raise AssertionError(f'Expected one {tag}, found {len(matches)}')
        return matches[0]

    def key(name, pressed):
        command = 'Input.' + ('+' if pressed else '-') + 'key ' + name
        ue.SystemLibrary.execute_console_command(world, command, pc)

    def tap(name):
        key(name, True)
        try:
            yield 0
        finally:
            key(name, False)
        yield .16

    def wait_for(condition, seconds):
        deadline = game.get_time_seconds(world) + seconds
        while not condition() and game.get_time_seconds(world) < deadline:
            yield .025
        return bool(condition())

    def place(point):
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(point, False, True)

    def aim(point):
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))

    def approach(point, offset):
        place(V(point.x + offset.x, point.y + offset.y, home_z))
        yield .18
        aim(point)
        yield .18

    def collect(kind, flag):
        actor = next(f for f in fixtures if str(prop(f, 'Kind')) == kind)
        center = bounds(prop(actor, 'Trigger'))[0]
        attempts = []
        for side in (V(0,97,0), V(0,-97,0), V(97,0,0), V(-97,0,0)):
            yield from approach(center, side)
            stable = 0
            # A freshly teleported pawn can still be settling. Re-aim from the
            # current camera until both the original trace and HUD agree on two ticks.
            for _ in range(18):
                aim(center)
                yield 0
                hit = pawn.call_method('LineTrace', (350.,))
                prompt = kit.get_prompt()
                stable = stable + 1 if hit == actor and prompt and prompt == actor.prompt(kit) else 0
                if stable == 2:
                    break
            attempts.append({'eye': xyz(camera.get_world_location()), 'target': hit.get_name() if hit else None,
                             'prompt': prompt, 'ready': stable == 2})
            if stable == 2:
                yield from tap('E')
                if prop(kit, flag) and not actor.get_actor_enable_collision():
                    break
        require(f'E collects the placed {kind} pickup', prop(kit, flag) and not actor.get_actor_enable_collision(),
                {'pickup': actor.get_name(), 'approaches': attempts, 'contact': xyz(center)})

    def lamp(on):
        if bool(prop(kit, 'bLampOn')) != on:
            yield from tap('F')
        if bool(prop(kit, 'bLampOn')) != on or beam.is_visible() != on:
            raise AssertionError(f'Puzzle lamp F failed: requested {on}, held {prop(kit, "Equipped")}')

    def filter_off(wanted):
        yield from lamp(False)
        for _ in range(5):
            if str(prop(kit, 'Filter')) == wanted:
                return
            yield from tap('Q')
        raise AssertionError(f'Q did not select {wanted}, observed {prop(kit, "Filter")}')

    def width(wanted):
        for _ in range(5):
            value = prop(kit, 'RadiusLevel')
            if value == wanted:
                return
            yield from tap('V' if value < wanted else 'C')
        raise AssertionError(f'Could not select beam width {wanted}')

    def extension(panel):
        return float(prop(panel, 'Extension'))

    def travel(panel):
        return float(prop(panel, 'Travel'))

    def settled(panel, extended):
        return (bool(prop(panel, 'bExtended')) == extended
                and abs(extension(panel) - (travel(panel) if extended else 0.)) < 1.5)

    require('Fresh showroom starts with the ordinary flashlight in hand',
            game.get_current_level_name(world) == 'L_ModelsAndMechanics_Showcase'
            and str(prop(kit, 'Equipped')) == 'StockLamp')
    walls = [tagged('LAB-MODULE-WALL-' + letter) for letter in 'ABC']
    middle = walls[1]
    floor = tagged('LAB-MODULE-FLOOR')
    crate = tagged('LAB-MODULE-CRATE-A')
    mesh = crate.get_component_by_class(ue.StaticMeshComponent)
    front = middle.get_actor_forward_vector()

    if scenario == 'red':
        yield from approach(face(middle), front * 270.)
        if not stock_lamp.is_visible():
            yield from tap('F')
        yield .4
        require('Lit walking flashlight leaves the bank retracted',
                stock_lamp.is_visible() and all(settled(p, False) for p in walls))

    yield from collect('ColorLamp', 'bHasColorLamp')
    inventory = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                     if c.get_class().get_name() == 'BPC_Inventory_C')
    digits = 'One Two Three Four Five Six Seven Eight'.split()
    for key_name, row in zip(digits, prop(inventory, 'InventorySlots')):
        item = prop(row, 'Item_3_140BEB7D45592192CDA69DB2EE378C9B')
        if item and item.get_name() == 'BP_Pack_ColorLamp_C':
            yield from tap(key_name)
            break
    require('Quick slot equips the collected puzzle flashlight', str(prop(kit, 'Equipped')) == 'ColorLamp')

    if scenario == 'red':
        yield from filter_off('Red')
        yield from approach(face(middle), front * 270.)
        yield from width(1)
        require('Unlit red filter leaves all three panels retracted', all(settled(p, False) for p in walls))
        # Hold a real opaque crate between the camera and receiver until the negative check completes.
        blocker = tagged('LAB-MODULE-CRATE-B')
        obstacle = blocker.get_component_by_class(ue.StaticMeshComponent)
        saved = blocker.get_actor_transform()
        obstacle.set_simulate_physics(False)
        try:
            reset_body(blocker, obstacle, kit.view_origin() + kit.view_direction() * 100.)
            yield .15
            yield from lamp(True)
            yield .4
            require('Real crate blocks an otherwise aligned red beam',
                    pawn.call_method('LineTrace', (350.,)) == blocker and all(settled(p, False) for p in walls),
                    {'crate_center': xyz(bounds(obstacle)[0]), 'extensions': [extension(p) for p in walls]})
        finally:
            blocker.set_actor_transform(saved, False, True)
            obstacle.set_simulate_physics(True)
        origins = [prop(p, 'Block').get_world_location() for p in walls]
        stock_charge = prop(battery, 'CurrentBatteryLevel')
        puzzle_charge = prop(kit, 'PuzzleCharge')
        require('Removing the obstruction lets the narrow beam fully extend only the center panel',
                (yield from wait_for(lambda: settled(middle, True), 3.))
                and all(settled(walls[i], False) for i in (0, 2)),
                [extension(p) for p in walls])
        yield .55
        require('Continuous red exposure holds the panel at full physical travel and uses puzzle fuel',
                settled(middle, True) and dot(prop(middle, 'Block').get_world_location() - origins[1], front)
                > travel(middle) - 2. and prop(kit, 'PuzzleCharge') < puzzle_charge
                and prop(battery, 'CurrentBatteryLevel') == stock_charge,
                {'extension': extension(middle), 'puzzle_before': puzzle_charge, 'puzzle_after': prop(kit, 'PuzzleCharge'),
                 'walking_before': stock_charge, 'walking_after': prop(battery, 'CurrentBatteryLevel')})
        aim(camera.get_world_location() + front * 400.)
        yield .6
        require('Looking away leaves red extension latched', settled(middle, True))
        aim(face(middle))
        require('A second narrow exposure retracts the same panel',
                (yield from wait_for(lambda: settled(middle, False), 3.)))
        yield from lamp(False)
        yield from width(5)
        aim(face(middle))
        before = bounds(mesh)[0]
        started = game.get_time_seconds(world)
        def push_sample():
            return {'t': round(game.get_time_seconds(world) - started, 3), 'panel': round(extension(walls[0]), 1),
                    'crate': round(dot(bounds(mesh)[0] - before, front), 1)}
        samples = [push_sample()]
        yield from lamp(True)
        while not all(settled(p, True) for p in walls) and game.get_time_seconds(world) < started + 3.:
            samples.append(push_sample())
            yield 0
        samples.append(push_sample())
        require('A single wide red exposure fully extends all three physical panels',
                all(settled(p, True) for p in walls)
                and all(dot(prop(p, 'Block').get_world_location() - origin, front) > travel(p) - 2.
                        for p, origin in zip(walls, origins)), [extension(p) for p in walls])
        yield .1
        samples.append(push_sample())
        after = bounds(mesh)[0]
        require('Wall motion pushes the placed, freely simulating crate outward',
                mesh.is_simulating_physics() and dot(after - before, front) > 22.,
                {'before': xyz(before), 'after': xyz(after), 'outward_cm': samples})
        return

    if scenario == 'cargo':
        yield from filter_off('Red')
        yield from width(3)
        start = face(floor)
        side = floor.get_actor_right_vector()
        half = bounds(mesh)[1]
        reset_body(crate, mesh, start + side * 43. + UP * (half.z + 2.))
        yield from approach(start, V(176., -30., 0.))
        before = bounds(mesh)[0]
        yield from lamp(True)
        require('Cargo standing on the moving floor rises with the extended panel',
                (yield from wait_for(lambda: settled(floor, True), 3.))
                and bounds(mesh)[0].z - before.z > travel(floor) * .7 and mesh.is_simulating_physics(),
                {'before': xyz(before), 'after': xyz(bounds(mesh)[0]), 'extension': extension(floor)})

        yield from filter_off('Green')
        yield from approach(face(middle), front * 230.)
        yield from lamp(True)
        side = middle.get_actor_right_vector()
        half = bounds(mesh)[1]
        separation = abs(front.x)*half.x + abs(front.y)*half.y
        reset_body(crate, mesh, face(middle) + side * 49. + front * (separation + 1.) + UP * 18.)
        attached = yield from wait_for(middle.has_cargo, 1.)
        where = bounds(mesh)[0]
        yield from lamp(False)
        yield .6
        require('Green catches a crate on the wall and holds it against gravity in darkness',
                attached and middle.has_cargo() and (bounds(mesh)[0] - where).length() < 4.,
                {'on_capture': xyz(where), 'in_darkness': xyz(bounds(mesh)[0])})
        top = where.z + bounds(mesh)[1].z
        half_height = pawn.capsule_component.get_scaled_capsule_half_height()
        place(V(where.x, where.y, top + half_height + 12.))
        yield .55
        feet = pawn.get_actor_location().z - half_height
        require('The attached crate supports the player as a solid step',
                pawn.character_movement.movement_mode == ue.MovementMode.MOVE_WALKING
                and abs(feet - top) < 5. and middle.has_cargo(),
                {'feet': feet, 'crate_top': top})
        yield from approach(face(middle), front * 230.)
        yield from filter_off('White')
        yield from lamp(True)
        released = yield from wait_for(lambda: not middle.has_cargo() and mesh.is_simulating_physics(), .8)
        fell = yield from wait_for(lambda: bounds(mesh)[0].z < where.z - 15., 1.)
        require('The puzzle lamp’s white filter releases the crate back to gravity',
                str(prop(kit, 'Equipped')) == 'ColorLamp' and released and fell,
                {'held': xyz(where), 'released': xyz(bounds(mesh)[0]), 'simulating': mesh.is_simulating_physics()})
        return

    if scenario == 'blue':
        yield from filter_off('Blue')
        yield from width(5)
        for panel, mounting in ((middle, 'wall'), (floor, 'floor')):
            normal = panel.get_actor_forward_vector()
            side = panel.get_actor_right_vector()
            target = face(panel)
            yield from approach(target, front * 225. if mounting == 'wall' else V(168., 0., 0.))
            yield from lamp(True)
            half = bounds(mesh)[1]
            depth = abs(normal.x)*half.x + abs(normal.y)*half.y + abs(normal.z)*half.z
            start = target + side * 44. + normal * (depth + 1.)
            count = panel.get_launch_count()
            reset_body(crate, mesh, start)
            launched = yield from wait_for(lambda: (dot(mesh.get_physics_linear_velocity(), normal) > 140.
                                                    and dot(bounds(mesh)[0] - start, normal) > 8.), 1.)
            require(f'Blue {mounting} launches a real physics body along the face normal',
                    launched and mesh.is_simulating_physics() and panel.get_launch_count() > count,
                    {'start': xyz(start), 'observed': xyz(bounds(mesh)[0]), 'velocity': xyz(mesh.get_physics_linear_velocity())})
            # This wall placement overlaps the edge of a second lit panel. Record the
            # next physics steps too, so impulses queued by both panels are observable.
            normal_speeds = [dot(mesh.get_physics_linear_velocity(), normal)]
            until = game.get_time_seconds(world) + .12
            while game.get_time_seconds(world) < until:
                yield 0
                normal_speeds.append(dot(mesh.get_physics_linear_velocity(), normal))
            require(f'Blue {mounting} contact stays within a single launch speed',
                    max(normal_speeds) < 950., {'peak_normal_speed_cm_s': round(max(normal_speeds), 2)})
            yield from lamp(False)
            # Bring the capsule into actual surface contact with the beam off, then use F.
            cap = pawn.capsule_component
            if mounting == 'floor':
                start = target + side * 21. + normal * (cap.get_scaled_capsule_half_height() + 1.)
            else:
                start = target + normal * (cap.get_scaled_capsule_radius() + 1.)
                start.z = home_z
            place(start)
            yield .12
            aim(target)
            yield .12
            start = pawn.get_actor_location()
            count = panel.get_launch_count()
            key('F', True)
            try:
                yield 0
            finally:
                key('F', False)
            launched = yield from wait_for(lambda: (dot(pawn.get_velocity(), normal) > 125.
                                                    and dot(pawn.get_actor_location() - start, normal) > 8.), 1.)
            require(f'Blue {mounting} launches the player along the face normal',
                    launched and panel.get_launch_count() > count,
                    {'start': xyz(start), 'observed': xyz(pawn.get_actor_location()), 'velocity': xyz(pawn.get_velocity())})
            yield from lamp(False)
        return

    if scenario != 'guide':
        raise ValueError(f'Unknown light module scenario {scenario}')
    guide = tagged('LAB-MODULE-GUIDE')
    ball = tagged('LAB-MODULE-BALL')
    sphere = ball.get_component_by_class(ue.StaticMeshComponent)
    block = prop(guide, 'Block')
    yield from filter_off('UV')
    yield from approach(face(guide), V(225., -200., 0.))
    require('Guide has an installed simulated ball', sphere.is_simulating_physics())
    center = bounds(block)[0]
    # Release above the entire trough, including its downhill slope and raised side rails.
    guide_top = max(bounds(c)[0].z + bounds(c)[1].z for c in guide.get_components_by_class(ue.StaticMeshComponent))

    def release_ball(pocket, label):
        # A vertical release with no initial horizontal velocity isolates the physical slope.
        # Chaos uses an analytic sphere. Its radius is constant even as the rendered
        # mesh's world-space bounding box changes size during a roll.
        low, high = sphere.get_local_bounds()
        radius = (high.x-low.x) * .5 * abs(ball.get_actor_scale3d().x)
        start = V(center.x, center.y, guide_top + radius + 4.)
        sphere.set_simulate_physics(False)
        reset_body(ball, sphere, start)
        yield .10
        sphere.set_simulate_physics(True)
        sphere.set_physics_linear_velocity(ZERO)
        sphere.set_physics_angular_velocity_in_degrees(ZERO)
        sphere.wake_all_rigid_bodies()
        deadline = game.get_time_seconds(world) + 3.
        while game.get_time_seconds(world) < deadline:
            now = bounds(sphere)[0]
            horizontal = V(now.x - start.x, now.y - start.y, 0.)
            if horizontal.length() >= 47.:
                break
            yield .025
        else:
            raise AssertionError(f'Ball failed to roll 47 cm: release {xyz(start)}, final {xyz(bounds(sphere)[0])}')
        base = tagged('LAB-MODULE-POCKET-' + pocket + '-BASE').static_mesh_component
        base_center, base_extent = bounds(base)
        floor_z = base_center.z + base_extent.z
        end = game.get_time_seconds(world) + 14.
        rested_since = None
        reached = False
        while game.get_time_seconds(world) < end:
            now = bounds(sphere)[0]
            velocity = sphere.get_physics_linear_velocity().length()
            inside = (abs(now.x - base_center.x) <= base_extent.x and
                      abs(now.y - base_center.y) <= base_extent.y and
                      abs(now.z - radius - floor_z) < 3.)
            reached |= inside
            if inside and velocity < 6.:
                if rested_since is None:
                    rested_since = game.get_time_seconds(world)
                if game.get_time_seconds(world) - rested_since >= .65:
                    break
            else:
                rested_since = None
            yield .05
        rested = rested_since is not None and game.get_time_seconds(world) - rested_since >= .65
        check(f'The ball rolls into pocket {label} and remains at rest on its floor', rested,
              {'entered': reached, 'final_center': xyz(bounds(sphere)[0]), 'tray_center': xyz(base_center),
               'radius_cm': round(radius,2), 'floor_z': round(floor_z,2),
               'speed_cm_s': round(sphere.get_physics_linear_velocity().length(), 2)})
        return horizontal

    first = yield from release_ball('N', 'A')
    initial = int(prop(guide, 'QuarterTurns'))
    starting_axis = block.get_right_vector()
    yield from lamp(True)
    yield .8
    expect = (initial + 1) % 4
    turned_axis = block.get_right_vector()
    require('A continuous purple exposure rotates the physical guide through exactly one quarter turn',
            int(prop(guide, 'QuarterTurns')) == expect and abs(dot(starting_axis, turned_axis)) < .15,
            {'before': xyz(starting_axis), 'after': xyz(turned_axis), 'quarter_turns': int(prop(guide, 'QuarterTurns'))})
    yield from lamp(False)
    second = yield from release_ball('W', 'B')
    cosine = dot(first, second) / (first.length() * second.length())
    require('Identical releases from rest send the ball along perpendicular paths after UV rotation',
            abs(cosine) < .30, {'first_cm': xyz(first), 'second_cm': xyz(second),
                                'angle_degrees': round(math.degrees(math.acos(max(-1., min(1., cosine)))), 2)})
    yield from lamp(True)
    yield .8
    require('A fresh purple exposure rotates the guide another quarter turn',
            int(prop(guide, 'QuarterTurns')) == (expect + 1) % 4 and dot(block.get_right_vector(), starting_axis) < -.9,
            {'after': xyz(block.get_right_vector()), 'quarter_turns': int(prop(guide, 'QuarterTurns'))})
    yield from lamp(False)
    yield from release_ball('S', 'C')
    yield from lamp(True)
    yield .8
    yield from lamp(False)
    yield from release_ball('E', 'D')
