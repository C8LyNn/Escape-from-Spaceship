"""PIE regression for the showroom's original backpack, equipped hand, and puzzle inputs."""
import re
import traceback

import unreal as ue


DIGITS = 'Zero One Two Three Four Five Six Seven Eight Nine'.split()
ITEM = 'Item_3_140BEB7D45592192CDA69DB2EE378C9B'
AMOUNT = 'Amount_5_F2E3DFDB40686114A88AD29A3674A46E'


def cases(world, check):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    fixtures = list(game.get_all_actors_of_class(world, ue.ShipFixture))
    camera = pawn.get_component_by_class(ue.CameraComponent)
    home = pawn.get_actor_location()
    prop = lambda obj, name: obj.get_editor_property(name)
    inv = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
               if c.get_class().get_name() == 'BPC_Inventory_C')
    menu = prop(pc, 'W_InventoryMenuRef')
    slot_cls = ue.load_class(None, '/Game/UMG/Inventory/W_InventorySlot.W_InventorySlot_C')
    now = lambda: game.get_time_seconds(world)
    in_hand = lambda: str(prop(kit, 'Equipped'))
    charges = lambda: int(prop(kit, 'GlowstickCharges'))
    sticks = lambda: list(game.get_all_actors_of_class(world, ue.ShipGlowstick))

    def rows():
        result = []
        for slot in prop(inv, 'InventorySlots'):
            item = prop(slot, ITEM)
            result.append((item.get_name() if item else '', int(prop(slot, AMOUNT))))
        return result

    def pack(kind):
        return 'BP_Pack_' + kind + '_C'

    def index_of(name):
        return next(i for i, (cls, count) in enumerate(rows()) if cls == name and count > 0)

    def tap(key):
        ue.SystemLibrary.execute_console_command(world, 'Input.+key ' + key, pc)
        try:
            yield 0
        finally:
            ue.SystemLibrary.execute_console_command(world, 'Input.-key ' + key, pc)
        yield .25

    def place(point):
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(point, False, True)

    def turn_to(point):
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))

    def fixture(kind):
        return next(a for a in fixtures if str(prop(a, 'Kind')) == kind)

    def face_fixture(actor):
        point = ue.SystemLibrary.get_component_bounds(prop(actor, 'Trigger'))[0]
        for side in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            place(ue.Vector(point.x + side[0] * 97, point.y + side[1] * 97, home.z))
            yield .18
            turn_to(point)
            yield .2
            if kit.get_prompt() == actor.prompt(kit):
                return True
        return False

    def operate(actor, worked, label):
        if (yield from face_fixture(actor)):
            yield from tap('E')
            if worked():
                check(label + ' via E', True)
                return
        check(label + ' via E', False, {'prompt': kit.get_prompt(), 'expected': actor.prompt(kit)})
        # Preserve the failed input result while exercising the rest of the inventory independently.
        actor.use(kit)
        yield .18
        check(label + ' via fixture fallback', worked())

    def open_menu():
        if not menu.is_visible():
            yield from tap('Tab')
        if not menu.is_visible() or not pc.show_mouse_cursor:
            raise AssertionError('Tab did not open the original controller-owned inventory menu')

    def close_menu():
        if menu.is_visible():
            yield from tap('Tab')
        if menu.is_visible():
            raise AssertionError('Original inventory stayed open after Tab')

    def swap(index, click=False, menu_digit=False):
        if click:
            yield from open_menu()
            widgets = [w for w in ue.WidgetLibrary.get_all_widgets_of_class(world, slot_cls, False)
                       if prop(w, 'Index') == index and w.get_owning_player() == pc]
            if len(widgets) != 1:
                raise AssertionError(f'Expected one original slot widget at {index}, found {len(widgets)}')
            prop(widgets[0], 'SlotButton').on_released.broadcast()
            yield .15
            yield from close_menu()
        elif menu_digit:
            yield from open_menu()
            yield from tap(DIGITS[index + 1])
            check('Number in Tab inventory returns to gameplay',
                  not menu.is_visible() and not pc.show_mouse_cursor and not pc.is_move_input_ignored())
            yield from close_menu()
        else:
            yield from close_menu()
            yield from tap(DIGITS[index + 1])

    def equip(kind, click=False, menu_digit=False):
        if in_hand() == kind:
            return
        old_hand, before = in_hand(), rows()
        index = index_of(pack(kind))
        yield from swap(index, click, menu_digit)
        after = rows()
        expected = before.copy()
        expected[index] = ('', 0) if old_hand == 'None' else (pack(old_hand), 1)
        route = 'Tab number' if menu_digit else 'SlotButton' if click else 'Number key'
        check(route + ' equips ' + kind,
              in_hand() == kind and after == expected,
              {'slot': index + 1, 'hand': in_hand(), 'before': before, 'after': after})

    def pickups():
        check('Showroom opens with original pawn, eight real slots, and stock lamp in hand',
              game.get_current_level_name(world) == 'L_ModelsAndMechanics_Showcase'
              and pawn.get_class().get_name() == 'L1_Character_C' and len(rows()) == 8
              and in_hand() == 'StockLamp' and not menu.is_visible(),
              {'map': game.get_current_level_name(world), 'hand': in_hand(), 'slots': rows()})
        bandage_path = '/Game/Blueprints/Actors/Inventory/BP_InventoryItem_Bandage'
        bandage = ue.load_class(None, bandage_path + '.BP_InventoryItem_Bandage_C')
        inv.call_method('AddItem', (bandage, 8))
        full = rows()
        welder = fixture('Welder')
        welder.use(kit)
        check('A full backpack leaves the pickup usable and preserves every slot',
              all(count == 1 for _,count in full) and rows() == full
              and not prop(welder,'bActive') and welder.get_actor_enable_collision())
        for i in range(8):
            inv.call_method('RemoveItem', (i,))
        for kind in ('White', 'ColorLamp', 'Welder', 'Battery', 'Card', 'Glowstick', 'Note'):
            actor = fixture(kind)
            before = rows()
            yield from operate(actor, lambda: (pack(kind), 1) in rows(), 'Collect ' + kind)
            after = rows()
            changed = [i for i, pair in enumerate(zip(before, after)) if pair[0] != pair[1]]
            check(kind + ' enters one original slot and leaves the world',
                  len(changed) == 1 and before[changed[0]][1] == 0 and after[changed[0]] == (pack(kind), 1)
                  and in_hand() == 'StockLamp' and bool(prop(actor, 'hidden')) and not actor.get_actor_enable_collision(),
                  {'hand': in_hand(), 'changed': changed, 'slots': after})
            if pc.is_move_input_ignored() and not menu.is_visible():
                yield from tap('Tab')

    def inventory():
        # Stock bandages normally occupy a single slot. The eighth slot tests a completely full bag.
        bandage_path = '/Game/Blueprints/Actors/Inventory/BP_InventoryItem_Bandage'
        bandage = ue.load_class(None, bandage_path + '.BP_InventoryItem_Bandage_C')
        inv.call_method('AddItem', (bandage, 1))
        check('Original consumable shares the last available slot with the lab equipment',
              sum(count > 0 for _, count in rows()) == 8 and (bandage.get_name(), 1) in rows(), rows())
        yield from equip('White', click=True)
        yield from equip('Welder', menu_digit=True)
        yield from equip('Battery', click=True)
        yield from equip('Card')
        yield from equip('ColorLamp', click=True)

        # Seed a multi-count row through the same stock AddItem operation that collects pickups.
        # Restore its shared CDO immediately, before the next input, game tick, or save.
        index = index_of(bandage.get_name())
        defaults = ue.get_default_object(bandage)
        data = prop(defaults, 'ItemData')
        maxfield = 'MaxStackAmount_8_585E54064FCB31E972D6CCB02D1947CC'
        previous_max = prop(data, maxfield)
        try:
            data.set_editor_property(maxfield, 3)
            defaults.set_editor_property('ItemData', data)
            inv.call_method('AddItem', (bandage, 2))
        finally:
            data.set_editor_property(maxfield, previous_max)
            defaults.set_editor_property('ItemData', data)
        before = rows()
        check('Stock AddItem seeded three bandages and original stack limit was restored',
              before[index] == (bandage.get_name(), 3) and prop(prop(defaults, 'ItemData'), maxfield) == previous_max)
        yield from swap(index, click=True)
        held = rows()
        yield from swap(index, click=False)
        check('Selected slot and hand exchange the entire stock stack in a full bag',
              held[index] == (pack('ColorLamp'), 1) and rows() == before and rows()[index][1] == 3
              and in_hand() == 'ColorLamp', {'index': index, 'before': before, 'in_hand_step': held, 'after': rows()})

    def welding():
        yield from equip('Welder')
        hand = next(c for c in kit.get_components_by_class(ue.StaticMeshComponent) if c.get_name() == 'HeldItemMesh')
        mesh = hand.static_mesh
        check('Visible camera-mounted WD01 has its model-specific muzzle socket',
              hand.is_visible() and hand.get_attach_parent() == camera and mesh and mesh.get_name() == 'SM_WD01_RepairWelder'
              and mesh.find_socket('Muzzle') and mesh.find_socket('Hand')
              and (hand.get_socket_location('Muzzle') - hand.get_socket_location('Hand')).length() > 30.,
              {'mesh': mesh.get_path_name() if mesh else None})
        before = rows()
        yield from tap('Q')
        yield from tap('MouseScrollUp')
        check('Filter controls preserve the equipped welder and its backpack slot',
              in_hand() == 'Welder' and rows() == before)
        door = fixture('RepairDoor')
        if not (yield from face_fixture(door)):
            raise AssertionError('Could not aim at the repairable door')
        duration = float(prop(door, 'RepairTime'))
        charge = float(prop(kit, 'Charge'))
        ue.SystemLibrary.execute_console_command(world, 'Input.+key E', pc)
        try:
            yield duration * .35
            progress = prop(door, 'Progress')
            check('Held E starts a powered weld', prop(kit, 'bWelding') and 0 < progress < 1,
                  {'progress': progress, 'charge': prop(kit, 'Charge')})
            yield duration * .65 + .4
        finally:
            ue.SystemLibrary.execute_console_command(world, 'Input.-key E', pc)
        yield .25
        check('Completed WD01 repair opens the actual leaf and uses the backpack battery',
              prop(door, 'bSolved') and prop(door, 'bOpen') and not prop(kit, 'bWelding')
              and prop(kit, 'Charge') < charge and (pack('Battery'), 1) in rows(),
              {'before': charge, 'after': prop(kit, 'Charge')})

    def carried_bandage():
        place(home)
        yield .2
        name = 'BP_InventoryItem_Bandage_C'
        index = index_of(name)
        qty = rows()[index][1]
        original = in_hand()
        yield from swap(index, click=True)
        check('Stock bandage moves from selected slot into hand', in_hand() == 'Bandage' and not any(c == name for c, _ in rows()))
        health = next(c for c in pawn.get_components_by_class(ue.ActorComponent) if c.get_class().get_name() == 'BPC_Health_C')
        game.apply_damage(pawn, 15., pc, kit, ue.DamageType)
        yield .1
        partial = float(prop(health, 'CurrentHealth'))
        maximum = float(prop(health, 'MaxHealth'))
        yield from tap('LeftMouseButton')
        healed = float(prop(health, 'CurrentHealth'))
        yield from swap(index)
        expected = (name, qty - 1) if qty > 1 else ('', 0)
        check('LMB uses one held bandage and the rest returns to its stock slot',
              partial < healed and abs(healed - maximum) < .2 and rows()[index] == expected and in_hand() == original,
              {'stack_before': qty, 'returned': rows()[index], 'before': partial, 'after': healed, 'max': maximum})

    def carried_key():
        # Free the stock-consumable slot with its public inventory operation for this fresh key-door scenario.
        rows_before = rows()
        if not any(qty == 0 for _, qty in rows_before):
            bandage = 'BP_InventoryItem_Bandage_C'
            slot = index_of(bandage)
            for _ in range(rows_before[slot][1]):
                inv.call_method('RemoveItem', (slot,))
        item = ue.load_class(None, '/Game/Blueprints/Actors/Inventory/BP_InventoryItem_DoorKey.BP_InventoryItem_DoorKey_C')
        inv.call_method('AddItem', (item, 1))
        index = index_of(item.get_name())
        yield from swap(index, click=True)
        before = rows()
        check('Only held key contributes to the stock key count',
              in_hand() == 'DoorKey' and prop(pawn, 'KeyCollected') == 1 and all(c != item.get_name() for c, _ in before))
        door_cls = ue.load_class(None, '/Game/Blueprints/Actors/Interactions/BP_Interaction_OutwardDoor.BP_Interaction_OutwardDoor_C')
        door = next(d for d in game.get_all_actors_of_class(world, door_cls)
                    if prop(d, 'IsDoorClosed') and not d.actor_has_tag('Inventory.DoorUnlocked'))
        leaf = next(c for c in door.get_components_by_class(ue.StaticMeshComponent)
                    if c.static_mesh and c.static_mesh.get_name().lower() in ('sm_door', 'sm_door_showcase'))
        center, extent, _ = ue.SystemLibrary.get_component_bounds(leaf)
        normal, right = door.get_actor_forward_vector(), door.get_actor_right_vector()
        for side in (1, -1):
            for across, height in ((0., 0.), (.68, .45), (-.68, .45), (0., -.55), (.64, -.55)):
                target = center + right * (across * max(extent.x, extent.y)) + ue.Vector(0, 0, height * extent.z)
                near = target - normal * (97 * side)
                place(ue.Vector(near.x, near.y, home.z))
                yield .2
                turn_to(target)
                yield .2
                if pawn.call_method('LineTrace', (350.,)) == door:
                    yield from tap('E')
                    yield .85
                    if door.actor_has_tag('Inventory.DoorUnlocked'):
                        break
            if door.actor_has_tag('Inventory.DoorUnlocked'):
                break
        check('E unlocks and opens the outward door with only a key in hand',
              door.actor_has_tag('Inventory.DoorUnlocked') and not prop(door, 'IsDoorClosed'))
        if not door.actor_has_tag('Inventory.DoorUnlocked'):
            door.call_method('Interact')
            yield .9
        check('Stock door consumes the held key exactly once',
              door.actor_has_tag('Inventory.DoorUnlocked') and in_hand() == 'None'
              and prop(pawn, 'KeyCollected') == 0 and rows() == before,
              {'door': door.get_name(), 'hand': in_hand(), 'stock_key_count': prop(pawn, 'KeyCollected')})
        # A previous door test may leave the view beside a doorway. Return to a neutral spot.
        place(home)
        yield .15

    def journal():
        yield from equip('Note', click=True)
        note = fixture('Note')
        story = str(prop(note, 'Value')) or str(prop(note, 'Caption'))
        before = list(prop(kit, 'Notes'))
        for attempt in range(2):
            yield from tap('LeftMouseButton')
            check('Read carried note ' + str(attempt + 1), pc.is_move_input_ignored() and not menu.is_visible()
                  and str(prop(kit, 'LastNote')) == story and list(prop(kit, 'Notes')) == before,
                  {'note': str(prop(kit, 'LastNote')), 'pages': len(before)})
            yield from tap('Tab')
            check('Close carried note ' + str(attempt + 1), not pc.is_move_input_ignored() and not menu.is_visible())

    thrown = []

    def glowsticks():
        yield from equip('Glowstick')
        place(home)
        pc.set_control_rotation(ue.Rotator(roll=0., pitch=-9., yaw=0.))
        yield .2
        full = charges()
        if full <= 0 or full > 16:
            raise AssertionError(f'Invalid initial glowstick reserve: {full}')
        existing = set(sticks())
        started = now()
        for n in range(full):
            before = set(sticks())
            yield from tap('LeftMouseButton')
            new = set(sticks()) - before
            check('Throw ' + str(n + 1) + ' uses exactly one stick',
                  len(new) == 1 and charges() == full - n - 1, {'spawned': len(new), 'reserve': charges()})
            if len(new) == 1:
                thrown.extend(new)
        if len(thrown) != full:
            raise AssertionError('Missing thrown actors')
        first = thrown[0]
        light = first.get_component_by_class(ue.PointLightComponent)
        movement = first.get_component_by_class(ue.ProjectileMovementComponent)
        meshes = first.get_components_by_class(ue.StaticMeshComponent)
        check('Throwable contains visible geometry, bouncing movement and a working scene light',
              len(meshes) >= 3 and all(m.static_mesh for m in meshes) and movement and movement.should_bounce
              and light and light.is_visible() and light.intensity > 0 and light.attenuation_radius > 150
              and 38 < first.get_life_span() <= 45, {'meshes': len(meshes), 'light': light.intensity if light else None})
        yield from tap('LeftMouseButton')
        check('Empty reserve does not spawn another actor', charges() == 0 and set(sticks()) == existing.union(thrown))
        yield max(0., started + 10.5 - now())
        check('No early recharge', charges() == 0, charges())
        at_rest = first.get_actor_location()
        yield .48
        check('Thrown glowstick settles at a stable position',
              (first.get_actor_location() - at_rest).length() < 1. and (at_rest - home).length() > 25., str(at_rest))
        yield max(0., started + 12.8 - now())
        check('One glowstick regenerates after twelve seconds', charges() == 1, charges())

    def plates(count):
        letters = 'ABCD'[:count]
        group = [next(a for a in fixtures if a.actor_has_tag(f'LAB-SEQ{count}-{letter}')) for letter in letters]
        doors = [d for d in fixtures if str(prop(d, 'Kind')) == 'Door' and prop(d, 'Channel') == prop(group[0], 'Channel')]
        for step, (letter, plate) in enumerate(zip(letters, group), 1):
            point = plate.get_actor_location()
            place(ue.Vector(point.x, point.y, home.z - 8.))
            yield .45
            progress = [float(prop(p, 'Progress')) for p in group]
            check(f'{letters} step {letter}', all(abs(v - step / count) < .01 for v in progress), progress)
        check(letters + ' opens its linked door', all(prop(p, 'bSolved') for p in group)
              and doors and all(prop(d, 'bOpen') for d in doors), [d.get_name() for d in doors])

    def keypad():
        panel = next(a for a in fixtures if a.actor_has_tag('LAB-FILTER-PAD'))
        glyphs = sorted((a for a in fixtures if str(prop(a, 'Kind')) == 'Glyph'
                         and prop(a, 'Channel') == prop(panel, 'Channel')), key=lambda a: prop(a, 'Order'))
        digits = [re.search(r'(\d)\s*$', str(prop(a, 'Value'))) for a in glyphs]
        if len(digits) != 4 or not all(digits):
            raise AssertionError('Four ordered glyphs must end in a code digit')
        code = ''.join(match.group(1) for match in digits)
        before = rows(), in_hand()
        yield from operate(panel, kit.is_keypad_open, 'Open filter keypad')
        for digit in code:
            yield from tap(DIGITS[int(digit)])
        yield from tap('Enter')
        doors = [a for a in fixtures if str(prop(a, 'Kind')) == 'Door' and prop(a, 'Channel') == prop(panel, 'Channel')]
        check('Glyph-derived keypad digits open the linked door without changing inventory',
              prop(panel, 'bSolved') and not kit.is_keypad_open() and doors and all(prop(d, 'bOpen') for d in doors)
              and before == (rows(), in_hand()), {'code': code, 'doors': [d.get_name() for d in doors]})

    def expiry():
        if not thrown:
            raise AssertionError('No thrown stick available for lifetime check')
        first = thrown[0]
        light = first.get_component_by_class(ue.PointLightComponent)
        full_strength = light.intensity
        yield max(0., first.get_life_span() - 4.)
        check('Ground light fades before expiry', 0 < light.intensity < full_strength * .8,
              {'full': full_strength, 'fading': light.intensity})
        yield max(0., first.get_life_span() + .4)
        check('Spent actor and its point light expire', not ue.SystemLibrary.is_valid(first) and not ue.SystemLibrary.is_valid(light))

    for name, test in [('pickups', pickups), ('inventory', inventory), ('WD01 repair', welding),
                       ('held bandage', carried_bandage), ('held key', carried_key), ('journal', journal), ('glowsticks', glowsticks),
                       ('three plates', lambda: plates(3)), ('four plates', lambda: plates(4)),
                       ('glyph keypad', keypad), ('glowstick lifetime', expiry)]:
        try:
            yield from test()
        except Exception:
            check(name + ' exception', False, traceback.format_exc())
