"""Check the imported props against the playable pawn, both on the bench and after equipping."""
import unreal as ue


def cases(world, check):
    game = ue.GameplayStatics
    pawn = game.get_player_pawn(world, 0)
    controller = game.get_player_controller(world, 0)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    capsule = pawn.get_component_by_class(ue.CapsuleComponent)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    held = next(c for c in kit.get_components_by_class(ue.StaticMeshComponent) if c.get_name() == 'HeldItemMesh')
    home = pawn.get_actor_location()
    eye_height = camera.get_world_location().z - (home.z - capsule.get_scaled_capsule_half_height())
    check('Standing eye height is measured from the player capsule sole', 110 < eye_height < 210, eye_height)

    def tap(key):
        ue.SystemLibrary.execute_console_command(world, 'Input.+key ' + key, controller)
        yield 0
        ue.SystemLibrary.execute_console_command(world, 'Input.-key ' + key, controller)
        yield .25

    def unit_scale(comp):
        return (comp.get_world_scale() - ue.Vector(1, 1, 1)).length() < .001

    def length(mesh):
        bb = mesh.get_bounding_box()
        size = bb.max - bb.min
        return max(size.x, size.y, size.z)

    check('The starting flashlight uses its native length in hand',
          held.static_mesh is not None and unit_scale(held) and 35 < length(held.static_mesh) < 37)
    inventory = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                     if c.get_class().get_name() == 'BPC_Inventory_C')
    digits = 'One Two Three Four Five Six Seven Eight'.split()
    for tag, item in [('LAB-CHARGER', None), ('LAB-BRIDGE', None), ('LAB-FILTER-PAD', None),
                      ('LAB-COLOR-LAMP', 'ColorLamp'), ('LAB-BATTERY', 'Battery'),
                      ('LAB-CARD', 'Card'), ('LAB-NOTE', 'Note')]:
        fixture, = game.get_all_actors_with_tag(world, tag)
        comp = fixture.get_editor_property('Mesh')
        mesh = comp.static_mesh
        center, _, _ = ue.SystemLibrary.get_component_bounds(comp)
        check(tag + ' displays the imported mesh at its native size', unit_scale(comp), length(mesh))
        targeted = False
        attempts = []
        for dx, dy in [(0, 110), (0, 145), (110, 25), (-110, 25)]:
            pawn.character_movement.stop_movement_immediately()
            pawn.set_actor_location(ue.Vector(center.x + dx, center.y + dy, home.z), False, True)
            yield .18
            for _ in range(6):
                controller.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), center))
                yield .09
                hit = pawn.call_method('LineTrace', (350.,))
                if hit == fixture and kit.get_prompt() == fixture.prompt(kit) and 'E' in kit.get_prompt():
                    targeted = True
                    break
            if targeted:
                break
            attempts.append((hit.get_actor_label() if hit else None, kit.get_prompt(),
                             str(camera.get_world_location()), str(camera.get_forward_vector())))
        else:
            trigger = fixture.get_editor_property('Trigger')
            raise AssertionError(f'{tag} could not be targeted. Mesh {center}, trigger {ue.SystemLibrary.get_component_bounds(trigger)}, views {attempts}')
        check(tag + ' can be targeted from the player camera', True, kit.get_prompt())
        if not item:
            continue
        yield from tap('E')
        slots = inventory.get_editor_property('InventorySlots')
        for index, slot in enumerate(slots):
            cls = slot.get_editor_property('Item_3_140BEB7D45592192CDA69DB2EE378C9B')
            if cls and cls.get_name() == 'BP_Pack_' + item + '_C':
                break
        else:
            raise AssertionError(f'{tag} did not enter the backpack through E')
        yield from tap('Tab')
        yield from tap(digits[index])
        check(item + ' keeps its pickup dimensions after selection from Tab',
              str(kit.get_editor_property('Equipped')) == item and held.static_mesh == mesh
              and unit_scale(held) and held.is_visible(), length(mesh))
