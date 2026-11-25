"""Showroom PIE regression for both backpack upgrades and the original inventory UI."""
import unreal as ue


ITEM = 'Item_3_140BEB7D45592192CDA69DB2EE378C9B'
AMOUNT = 'Amount_5_F2E3DFDB40686114A88AD29A3674A46E'
EMPTY = ('', 0)


def cases(world, check, direct_large=False):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    fixtures = game.get_all_actors_of_class(world, ue.ShipFixture)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    home = pawn.get_actor_location()
    heading = pc.get_control_rotation()
    inventory = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                     if c.get_class().get_name() == 'BPC_Inventory_C')
    menu = pc.get_editor_property('W_InventoryMenuRef')
    slot_class = ue.load_class(None, '/Game/UMG/Inventory/W_InventorySlot.W_InventorySlot_C')

    def stock_class(name):
        path = '/Game/Blueprints/Actors/Inventory/BP_InventoryItem_' + name
        return ue.load_class(None, path + '.BP_InventoryItem_' + name + '_C')

    bandage = stock_class('Bandage')
    key = stock_class('DoorKey')
    battery = stock_class('FlashlightBattery')
    bandage_row = bandage.get_name(), 1
    lamp_row = 'BP_Pack_StockLamp_C', 1

    def rows():
        contents = []
        for row in inventory.get_editor_property('InventorySlots'):
            item = row.get_editor_property(ITEM)
            contents.append((item.get_name() if item else '', row.get_editor_property(AMOUNT)))
        return contents

    def hand():
        return str(kit.get_editor_property('Equipped'))

    def page():
        return int(kit.get_editor_property('BackpackPage'))

    def require(label, condition, evidence=None):
        check(label, condition, evidence)
        if not condition:
            raise AssertionError(f'{label}: {evidence}')

    def tap(name):
        ue.SystemLibrary.execute_console_command(world, 'Input.+key ' + name, pc)
        try:
            yield 0
        finally:
            ue.SystemLibrary.execute_console_command(world, 'Input.-key ' + name, pc)
        yield .25

    def fixture(kind):
        return next(f for f in fixtures if str(f.get_editor_property('Kind')) == kind)

    def available(actor):
        return (ue.SystemLibrary.is_valid(actor) and not actor.get_editor_property('hidden')
                and actor.get_actor_enable_collision())

    def collected(actor):
        return not ue.SystemLibrary.is_valid(actor) or (
            actor.get_editor_property('hidden') and not actor.get_actor_enable_collision())

    def interact(actor):
        # Aim at the actual placed object, then send the same E down/up events as a player.
        center = ue.SystemLibrary.get_component_bounds(actor.get_editor_property('Trigger'))[0]
        for dx, dy in ((0, 97), (0, -97), (97, 0), (-97, 0)):
            pawn.character_movement.stop_movement_immediately()
            pawn.set_actor_location(ue.Vector(center.x + dx, center.y + dy, home.z), False, True)
            yield .18
            look = ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), center)
            pc.set_control_rotation(look)
            yield .2
            # Prompt alone can match a neighboring copy of the same pickup.
            if (pawn.call_method('LineTrace', (350.,)) == actor
                    and kit.get_prompt() == actor.prompt(kit)):
                yield from tap('E')
                return
        raise AssertionError(f'Could not target {actor.get_name()} at {center}; HUD: {kit.get_prompt()}')

    def upgrade(kind, capacity):
        actor = fixture(kind)
        before, held = rows(), hand()
        yield from interact(actor)
        expected = before + [EMPTY] * (capacity - len(before))
        require(f'{kind} upgrades full bag from {len(before)} to {capacity} through E',
                all(n > 0 for _, n in before) and rows() == expected
                and hand() == held and collected(actor), {'after': rows(), 'hand': hand()})
        yield from tap('E')
        require(f'Repeated E at the collected {kind} preserves contents and capacity',
                rows() == expected and hand() == held and collected(actor), rows())

    def visible_slots():
        grid = menu.get_editor_property('W_InventoryGrid').get_editor_property('InventoryGridPanel')
        result = []
        for i in range(grid.get_children_count()):
            widget = grid.get_child_at(i)
            if widget.get_class() == slot_class and widget.is_visible() and widget.get_owning_player() == pc:
                result.append(widget)
        return result

    def open_page(target):
        if not menu.is_visible():
            yield from tap('Tab')
        if not menu.is_visible() or not pc.show_mouse_cursor:
            raise AssertionError('Tab did not open the stock inventory')
        for _ in range(3):
            if page() == target:
                break
            yield from tap('PageDown' if page() < target else 'PageUp')
        indices = sorted(w.get_editor_property('Index') for w in visible_slots())
        expected = list(range(target * 8, target * 8 + 8))
        require(f'{len(rows())} slots, page {target + 1} shows all eight absolute indices',
                page() == target and indices == expected, indices)

    def end_slot(capacity, click):
        index = capacity - 1
        yield from open_page(index // 8)
        current = next(w for w in visible_slots() if w.get_editor_property('Index') == index)
        button = current.get_editor_property('SlotButton')
        if not button.get_is_enabled() or not button.is_visible():
            raise AssertionError(f'Occupied slot {capacity} is disabled or hidden')
        if click:
            button.on_released.broadcast()
            yield .25
        else:
            yield from tap('Eight')
        if menu.is_visible() or pc.show_mouse_cursor or pc.is_move_input_ignored():
            raise AssertionError(f'Selecting slot {capacity} did not return to gameplay')

    def reject_overflow(capacity):
        before, held = rows(), hand()
        inventory.call_method('AddItem', (bandage, 1))
        actor = fixture('Welder')
        yield from interact(actor)
        require(f'Full {capacity}-slot bag refuses the next stock item and leaves the welder collectible',
                len(before) == capacity and all(n > 0 for _, n in before)
                and rows() == before and hand() == held and available(actor), rows())

    def seed(item, amount):
        inventory.call_method('AddItem', (item, amount))

    require('Fresh showroom starts with eight empty stock slots and the stock lamp',
            game.get_current_level_name(world) == 'L_ModelsAndMechanics_Showcase'
            and rows() == [EMPTY] * 8 and hand() == 'StockLamp' and not menu.is_visible(), rows())
    seed(key, 1)
    seed(bandage, 6)
    seed(battery, 1)
    require('Original bag is full with three distinct item types',
            rows() == [(key.get_name(), 1)] + [bandage_row] * 6 + [(battery.get_name(), 1)], rows())
    if direct_large:
        yield from upgrade('LargeBackpack', 24)
        seed(bandage, 16)
        before, held = rows(), hand()
        small = fixture('SmallBackpack')
        for attempt in range(2):
            yield from interact(small)
            require(f'Smaller pickup after a direct large upgrade remains usable on attempt {attempt + 1}',
                    len(before) == 24 and all(n > 0 for _, n in before) and rows() == before
                    and hand() == held and available(small), {'slots': rows(), 'hand': hand()})
        return

    yield from reject_overflow(8)
    yield from upgrade('SmallBackpack', 16)
    seed(bandage, 7)
    glow = fixture('Glowstick')
    before = rows()
    yield from interact(glow)
    stocked = before[:-1] + [('BP_Pack_Glowstick_C', 1)]
    require('E stores the glowsticks in the last free slot at 16',
            before[-1] == EMPTY and rows() == stocked and collected(glow), rows())
    yield from reject_overflow(16)
    yield from open_page(1)
    yield from tap('MouseScrollDown')
    require('Mouse wheel stops at the last page of the small backpack', page() == 1)
    yield from tap('MouseScrollUp')
    require('Mouse wheel returns to page one without changing the held item',
            page() == 0 and menu.is_visible() and hand() == 'StockLamp')
    yield from open_page(0)
    yield from end_slot(16, click=True)
    swapped = stocked[:-1] + [lamp_row]
    require('The original slot 16 button exchanges its glowsticks for the held stock lamp',
            rows() == swapped and hand() == 'Glowstick', {'slots': rows(), 'hand': hand()})
    reserve = kit.get_editor_property('GlowstickCharges')
    sticks = set(game.get_all_actors_of_class(world, ue.ShipGlowstick))
    yield from tap('LeftMouseButton')
    spawned = set(game.get_all_actors_of_class(world, ue.ShipGlowstick)) - sticks
    require('The glowsticks taken from slot 16 throw a real projectile on LMB',
            len(spawned) == 1 and kit.get_editor_property('GlowstickCharges') == reserve - 1
            and rows() == swapped, {'spawned': len(spawned), 'reserve_before': reserve})
    yield from end_slot(16, click=False)
    require('Key 8 on page two restores slot 16 and the held lamp',
            rows() == stocked and hand() == 'StockLamp', rows())

    yield from upgrade('LargeBackpack', 24)
    before = rows()
    seed(bandage, 8)
    stocked = rows()
    require('Stock AddItem fills all eight new slots through slot 24',
            stocked == before[:16] + [bandage_row] * 8, stocked)
    yield from reject_overflow(24)
    yield from end_slot(24, click=False)
    swapped = stocked[:-1] + [lamp_row]
    require('Key 8 on page three exchanges only slot 24 for the held stock lamp',
            rows() == swapped and hand() == 'Bandage', {'slots': rows(), 'hand': hand()})
    health = next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                  if c.get_class().get_name() == 'BPC_Health_C')
    game.apply_damage(pawn, 15., pc, kit, ue.DamageType)
    yield .15
    wounded = health.get_editor_property('CurrentHealth')
    yield from tap('LeftMouseButton')
    require('The bandage from slot 24 heals on LMB and consumes one item',
            health.get_editor_property('CurrentHealth') > wounded and hand() == 'None'
            and rows() == swapped, {'before': wounded, 'after': health.get_editor_property('CurrentHealth')})
    yield from end_slot(24, click=True)
    require('The original slot 24 button restores the held lamp and leaves exactly slot 24 free',
            rows() == stocked[:-1] + [EMPTY] and hand() == 'StockLamp', rows())
    pawn.set_actor_location(home, False, True)
    pc.set_control_rotation(heading)
    yield .3
    seed(bandage, 1)
    pickup_path = '/Game/Blueprints/Actors/Inventory/PickupActors/BP_PickupActor_Bandage'
    pickup_type = ue.load_class(None, pickup_path + '.BP_PickupActor_Bandage_C')
    existing = set(game.get_all_actors_of_class(world, pickup_type))
    inventory.call_method('DropItem', (23,))
    dropped = set(game.get_all_actors_of_class(world, pickup_type)) - existing
    require('Stock DropItem releases only the bandage in slot 24',
            len(dropped) == 1 and rows() == stocked[:-1] + [EMPTY] and hand() == 'StockLamp', rows())
