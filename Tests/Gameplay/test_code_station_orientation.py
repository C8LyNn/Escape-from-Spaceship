"""Verify the three code stations from the approach used by a player in the showcase."""
import unreal as ue


def cases(world, check):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    survivor = game.get_actor_of_class(world, ue.ShipSurvivor)
    floor_z = pawn.get_actor_location().z
    digits = 'Zero One Two Three Four Five Six Seven Eight Nine'.split()

    def tap(key):
        ue.SystemLibrary.execute_console_command(world, 'Input.+key ' + key, pc)
        yield 0
        ue.SystemLibrary.execute_console_command(world, 'Input.-key ' + key, pc)
        yield .2

    def approach(actor, point, distance):
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(ue.Vector(point.x, point.y + distance, floor_z), False, True)
        yield .3
        for _ in range(5):
            pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))
            yield .1
            if pawn.call_method('LineTrace', (350.,)) == actor:
                return
        raise AssertionError(f'{actor.get_name()} cannot be targeted from the aisle')

    for tag in ('LAB-FILTER-PAD', 'LAB-CARD', 'LAB-BRIDGE'):
        actor, = game.get_all_actors_with_tag(world, tag)
        mesh = actor.get_editor_property('Mesh')
        target = ue.SystemLibrary.get_component_bounds(mesh)[0]
        yield from approach(actor, target, 110)
        if tag != 'LAB-CARD':
            check(tag + ' has its marked front facing the aisle', mesh.get_right_vector().y > .99)
        yield from tap('E')
        if tag == 'LAB-CARD':
            check('Bridge card can be collected on approach', survivor.get_editor_property('bHasCard'))
            continue
        check(tag + ' opens with E from the front', survivor.is_keypad_open())
        for digit in actor.get_editor_property('Value'):
            yield from tap(digits[int(digit)])
        yield from tap('Enter')
        check(tag + ' accepts its code', actor.get_editor_property('bSolved') and not survivor.is_keypad_open())
        if tag == 'LAB-FILTER-PAD':
            door, = game.get_all_actors_with_tag(world, 'LAB-FILTER-DOOR')
            check('Filter code opens the linked door', door.get_editor_property('bOpen'))

    lock, = [a for a in game.get_all_actors_with_tag(world, 'MECH-08') if a.get_class().get_name() == 'BP_Lock_C']
    view = lock.get_component_by_class(ue.CameraComponent)
    check('Combination lock camera looks forward from the aisle', view.get_forward_vector().y > .99)
    yield from approach(lock, lock.get_actor_location(), -110)
    yield from tap('E')
    yield 1.
    check('E enters the lock inspection camera from the aisle', pc.get_view_target() == lock)
    dials = list(lock.get_editor_property('Dials'))
    code = list(lock.get_editor_property('LockCombination'))
    check('Four dials face the inspection camera', len(dials) == len(code) == 4 and all(
        (dial.get_actor_location() - view.get_world_location()).dot(view.get_forward_vector()) > 0 for dial in dials))
    for dial, wanted in zip(dials, code):
        arrow = dial.get_editor_property('UpArrow')
        button = arrow.get_user_widget_object().get_editor_property('Button')
        for _ in range(10):
            if dial.get_editor_property('Number') == wanted:
                break
            button.on_pressed.broadcast()
            yield .55
    entered = [dial.get_editor_property('Number') for dial in dials]
    check('The four arrow controls set the lock combination', entered == code, entered)
    for _ in range(40):
        if pc.get_view_target() == pawn:
            break
        yield .2
    check('Solving the lock returns control to the pawn', pc.get_view_target() == pawn and not pc.is_look_input_ignored())
