"""Run with the Unreal Python console during a fresh showroom PIE session.

The test uses player input, moves the player between stands and restarts PIE's
current map once. Results are written to Saved/EquipmentHandoffs.json.
"""
import json
import time
import traceback
from pathlib import Path

import unreal as ue


def run():
    worlds = ue.EditorLevelLibrary.get_pie_worlds(False)
    if len(worlds) != 1:
        raise RuntimeError('Start a single-player PIE session in the mechanics showroom.')
    world = worlds[0]
    game = ue.GameplayStatics
    player = game.get_player_pawn(world, 0)
    controller = game.get_player_controller(world, 0)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    if game.get_current_level_name(world) != 'L_ModelsAndMechanics_Showcase' or not kit or kit.get_editor_property('bKitEnabled'):
        raise RuntimeError('Start from a fresh mechanics showroom before collecting the equipment.')
    camera = player.get_component_by_class(ue.CameraComponent)
    stock_light = player.get_component_by_class(ue.SpotLightComponent)
    beam = kit.get_component_by_class(ue.SpotLightComponent)
    battery = next(component for component in player.get_components_by_class(ue.ActorComponent)
                   if component.get_class().get_name() == 'BPC_Flashlight_C')
    fixtures = game.get_all_actors_of_class(world, ue.ShipFixture)
    menu = controller.get_editor_property('W_InventoryMenuRef')
    closed = menu.get_visibility()
    start_z = player.get_actor_location().z
    interval = battery.get_editor_property('DepletionSpeed')
    results = {'checks': {}, 'state': 'RUNNING'}
    path = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / 'EquipmentHandoffs.json'

    def save():
        path.write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')

    def check(name, passed, **evidence):
        results['checks'][name] = {'passed': bool(passed), **evidence}
        save()

    def key(name, down):
        game_input = 'Input.' + ('+' if down else '-') + 'key ' + name
        ue.SystemLibrary.execute_console_command(world, game_input, controller)

    def tap(name):
        key(name, True)
        yield 0
        key(name, False)
        yield .25

    def fixture(kind):
        return next(f for f in fixtures if str(f.get_editor_property('Kind')) == kind)

    def approach(point, offset):
        pos = point + offset
        player.character_movement.stop_movement_immediately()
        player.set_actor_location(ue.Vector(pos.x, pos.y, start_z), False, True)
        yield .2
        controller.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))
        yield .25

    def collect(kind):
        actor = fixture(kind)
        point = ue.SystemLibrary.get_component_bounds(actor.get_editor_property('Trigger'))[0]
        for offset in (ue.Vector(0,100,0), ue.Vector(0,-100,0), ue.Vector(100,0,0), ue.Vector(-100,0,0)):
            yield from approach(point, offset)
            if player.call_method('LineTrace', (350.,)) == actor and kit.get_prompt() == actor.prompt(kit):
                yield from tap('E')
                return
        raise AssertionError(f'Could not aim at {kind}: {kit.get_prompt()}')

    def stock_charge():
        return float(battery.get_editor_property('CurrentBatteryLevel'))

    def charge():
        return float(kit.get_editor_property('PuzzleCharge'))

    def check_adjustments(light, name):
        def values():
            return (light.intensity, light.outer_cone_angle)
        original = values()
        yield from tap('Z')
        dim = values()
        yield from tap('C')
        narrow = values()
        yield from tap('X')
        bright = values()
        yield from tap('V')
        check(name + ' responds to independent power and width controls',
              dim[0] < original[0] and dim[1] == original[1] and narrow[0] == dim[0]
              and narrow[1] < dim[1] and bright[0] > narrow[0] and bright[1] == narrow[1]
              and all(abs(v - w) < .01 for v, w in zip(values(), original)),
              original=original, dim=dim, narrow=narrow, bright=bright, restored=values())

    def near_ray():
        return kit.view_origin() + kit.view_direction() * 3

    def cases():
        if stock_light.is_visible():
            yield from tap('F')
        yield from tap('F')
        value = stock_charge()
        yield interval * 2 + .25
        check('Stock flashlight consumes its own battery', stock_light.is_visible() and stock_charge() < value)
        yield from check_adjustments(stock_light, 'Walking flashlight')
        yield from collect('White')
        yield from tap('One')
        before = stock_charge()
        was_spare = str(kit.get_editor_property('Equipped')) == 'FlashlightBattery'
        yield from tap('LeftMouseButton')
        check('Using the showroom spare cell refills only the walking flashlight and consumes the cell',
              was_spare and str(kit.get_editor_property('Equipped')) == 'None' and stock_charge() > before
              and charge() == 0 and kit.get_editor_property('Charge') == 0,
              before=before, after=stock_charge())
        yield from tap('One')
        yield from collect('ColorLamp')
        yield from tap('Two')
        yield from tap('F')
        yield .25
        check('Newly collected puzzle flashlight works without a welder battery',
              str(kit.get_editor_property('Equipped')) == 'ColorLamp' and beam.is_visible() and charge() > 0
              and not kit.get_editor_property('bHasBattery') and kit.get_editor_property('Charge') == 0)
        yield from check_adjustments(beam, 'Puzzle flashlight')
        value = stock_charge()
        puzzle_before = charge()
        yield interval * 2 + .35
        check('Lit puzzle flashlight drains its own cell while the stored walking flashlight keeps its charge',
              beam.is_visible() and charge() < puzzle_before and not stock_light.is_visible() and stock_charge() == value,
              before=value, after=stock_charge(), puzzle_before=puzzle_before, puzzle_after=charge())
        yield from tap('G')
        value = stock_charge()
        yield interval * 2 + .25
        check('Drawing the previously lit walking flashlight leaves it off and keeps its remaining charge',
              not stock_light.is_visible() and stock_charge() == value)
        yield from tap('F')
        yield interval * 2 + .25
        check('F restarts walking illumination and resumes its own drain', stock_light.is_visible() and stock_charge() < value)
        yield from tap('F')
        yield from tap('G')
        yield from tap('G')
        check('Returning to a stored unlit stock flashlight keeps it off', not stock_light.is_visible())
        yield from tap('G')
        if not kit.get_editor_property('bLampOn'):
            yield from tap('F')
        selected = str(kit.get_editor_property('Filter'))
        yield from tap('Tab')
        menu_open = menu.get_visibility() != closed
        paused = game.is_game_paused(world)
        check('Tab opens the stock inventory', menu_open)
        check('Backpack blocks player movement and camera input',
              controller.is_move_input_ignored() and controller.is_look_input_ignored())
        standing_height = player.get_actor_location().z
        yield from tap('SpaceBar')
        check('Pressing jump in the backpack leaves the player on the ground',
              abs(player.get_actor_location().z - standing_height) < 4,
              before=standing_height, after=player.get_actor_location().z)
        value, start = charge(), game.get_time_seconds(world)
        yield 1.1
        elapsed = game.get_time_seconds(world) - start
        expected = kit.get_lamp_drain_per_second() * elapsed
        check('Beam costs energy for game time spent in the inventory',
              beam.is_visible() and abs((value - charge()) - expected) < .09,
              seconds=elapsed, before=value, after=charge(), paused=paused)
        game.set_game_paused(world, True)
        value = charge()
        yield .65
        check('A real game pause freezes battery drain', game.is_game_paused(world) and charge() == value)
        game.set_game_paused(world, False)
        yield from tap('Tab')
        check('Closing Tab returns the selected color',
              menu.get_visibility() == closed and beam.is_visible() and str(kit.get_editor_property('Filter')) == selected)
        lock_class = ue.load_class(None, '/Game/Blueprints/Actors/Locks/BP_Lock.BP_Lock_C')
        lock = game.get_actor_of_class(world, lock_class)
        lock_mesh = lock.get_editor_property('Lock')
        point = ue.SystemLibrary.get_component_bounds(lock_mesh)[0]
        for direction in (lock.get_actor_forward_vector(), -lock.get_actor_forward_vector(),
                          lock.get_actor_right_vector(), -lock.get_actor_right_vector()):
            yield from approach(point, direction * 99)
            if player.call_method('LineTrace', (250.,)) == lock:
                yield from tap('E')
                yield 1.1
                if controller.get_view_target() == lock and controller.show_mouse_cursor:
                    break
        check('E enters the stock combination lock', controller.get_view_target() == lock and controller.show_mouse_cursor)
        value = charge()
        yield .6
        check('Lock inspection suspends light, sensors and battery together',
              not beam.is_visible() and not kit.beam_reaches(near_ray()) and charge() == value,
              visible=beam.is_visible(), lights_receiver=kit.beam_reaches(near_ray()), before=value, after=charge())
        yield from tap('Tab')
        check('Tab cannot open the backpack over the combination lock',
              controller.get_view_target() == lock and controller.show_mouse_cursor and menu.get_visibility() == closed)
        if menu.get_visibility() != closed:
            yield from tap('Tab')
        yield from tap('BackSpace')
        yield .65
        value = charge()
        yield .55
        check('Leaving the lock restores the selected beam and drain',
              controller.get_view_target() == player and beam.is_visible() and kit.beam_reaches(near_ray())
              and str(kit.get_editor_property('Filter')) == selected and charge() < value)
        yield from tap('Tab')
        kit.set_editor_property('PuzzleCharge', .18)
        yield .8
        check('An empty battery extinguishes the beam inside the backpack',
              charge() == 0 and not beam.is_visible() and not kit.get_editor_property('bLampOn'))
        yield from tap('Tab')
        yield from collect('Battery')
        check('Picking up the welder battery leaves the depleted puzzle flashlight empty',
              kit.get_editor_property('bHasBattery') and kit.get_editor_property('Charge') == 100 and charge() == 0)
        yield from tap('G')
        if not stock_light.is_visible():
            yield from tap('F')
        check('Walking flashlight still emits with the puzzle battery empty',
              stock_light.is_visible() and not beam.is_visible() and charge() == 0)
        yield from tap('G')
        before = stock_charge()
        yield from collect('Charger')
        check('Showroom charger restores the held puzzle lamp while walking charge stays unchanged',
              charge() == 100 and stock_charge() == before and str(kit.get_editor_property('Equipped')) == 'ColorLamp',
              walking_before=before, walking_after=stock_charge(), puzzle_after=charge())

        ladder = fixture('Ladder')
        point = ladder.get_actor_location() + ue.Vector(0, 0, 120)
        yield from approach(point, ladder.get_actor_forward_vector() * 100)
        yield from tap('E')
        key('W', True)
        yield .55
        key('W', False)
        yield .15
        check('E attaches and W climbs', kit.is_on_ladder() and player.get_actor_location().z > start_z + 30)
        yield from tap('Tab')
        z = player.get_actor_location().z
        key('W', True)
        yield .45
        key('W', False)
        yield from tap('Tab')
        final_z = player.get_actor_location().z
        check('Stock inventory preserves height on the ladder',
              kit.is_on_ladder() and abs(final_z - z) < 3 and menu.get_visibility() == closed
              and player.character_movement.movement_mode == ue.MovementMode.MOVE_NONE,
              before=z, after=final_z, mounted=kit.is_on_ladder(), menu_closed=menu.get_visibility() == closed,
              movement=str(player.character_movement.movement_mode))
        yield from tap('E')
        check('E still releases the ladder after closing inventory', not kit.is_on_ladder())
        yield .7
        here = player.get_actor_location()
        player.set_actor_location(here + ue.Vector(0, 0, 420), False, True)
        yield .12
        yield from tap('Tab')
        falling_from = player.get_actor_location().z
        yield .55
        check('Opening the backpack in midair allows gravity to act',
              menu.get_visibility() != closed and player.get_actor_location().z < falling_from - 30,
              before=falling_from, after=player.get_actor_location().z)
        yield from tap('Tab')
        controller.set_ignore_look_input(True)
        yield from tap('Tab')
        yield from tap('Tab')
        check('Closing the backpack preserves a separate look lock', controller.is_look_input_ignored())
        controller.set_ignore_look_input(False)
        hazard = fixture('Hazard').get_actor_location()
        player.set_actor_location(ue.Vector(hazard.x, hazard.y, start_z), False, True)
        yield .55
        check('Showroom hazard enters death state', kit.get_editor_property('bDead'))
        yield from tap('Tab')
        check('Tab cannot cover the death screen', menu.get_visibility() == closed and not controller.show_mouse_cursor)
        key('R', True)
        yield 0
        yield .75
        worlds = ue.EditorLevelLibrary.get_pie_worlds(False)
        new_player = game.get_player_pawn(worlds[0], 0) if worlds else None
        check('R restarts with a fresh character', new_player and new_player != player)
        if new_player and new_player != player:
            new_controller = game.get_player_controller(worlds[0], 0)
            ue.SystemLibrary.execute_console_command(worlds[0], 'Input.-key R', new_controller)
            new_kit = game.get_actor_of_class(worlds[0], ue.ShipSurvivor)
            new_menu = new_controller.get_editor_property('W_InventoryMenuRef')
            ue.SystemLibrary.execute_console_command(worlds[0], 'Input.+key Tab', new_controller)
            yield 0
            ue.SystemLibrary.execute_console_command(worlds[0], 'Input.-key Tab', new_controller)
            yield .25
            check('Stock inventory opens again after restart', new_menu.get_visibility() != closed)
            new_player.set_actor_location(ue.Vector(hazard.x, hazard.y, start_z), False, True)
            yield .65
            check('Death during stock inventory returns control to the ending',
                  new_kit.get_editor_property('bDead') and new_menu.get_visibility() == closed and
                  not new_controller.show_mouse_cursor and new_controller.is_move_input_ignored(),
                  menu_visible=new_menu.get_visibility() != closed, cursor=new_controller.show_mouse_cursor)

    generator = cases()
    next_time = 0
    busy = False

    def tick(delta):
        nonlocal next_time, busy
        if busy or time.monotonic() < next_time:
            return
        busy = True
        try:
            next_time = time.monotonic() + next(generator)
        except StopIteration:
            results['state'] = 'PASS' if all(c['passed'] for c in results['checks'].values()) else 'FAILED'
            save()
            ue.unregister_slate_post_tick_callback(handle)
        except Exception:
            results.update(state='ERROR', error=traceback.format_exc())
            save()
            ue.unregister_slate_post_tick_callback(handle)
        finally:
            busy = False

    save()
    handle = ue.register_slate_post_tick_callback(tick)


if __name__ == '__main__':
    run()
