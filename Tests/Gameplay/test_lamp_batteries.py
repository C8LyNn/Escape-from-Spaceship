"""Switch the two flashlights through the stock backpack and exercise their actual energy sources in PIE."""
import unreal as ue


SLOT_ITEM = 'Item_3_140BEB7D45592192CDA69DB2EE378C9B'
SLOT_AMOUNT = 'Amount_5_F2E3DFDB40686114A88AD29A3674A46E'
ITEM_NAME = 'ItemName_2_6D49EE6B4F51BF44ACC9EEB91FE10317'
ITEM_ICON = 'Icon_5_5B6A4C214B60C9FAC2E638A6996293D9'
DIGITS = 'One Two Three Four Five Six Seven Eight'.split()


def cases(world, check):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    fixtures = game.get_all_actors_of_class(world, ue.ShipFixture)
    prop = lambda obj, name: obj.get_editor_property(name)
    menu = prop(pc, 'W_InventoryMenuRef')
    deck_height = pawn.get_actor_location().z

    def pawn_part(class_name):
        return next(c for c in pawn.get_components_by_class(ue.ActorComponent)
                    if c.get_class().get_name() == class_name)

    inv = pawn_part('BPC_Inventory_C')
    cell = pawn_part('BPC_Flashlight_C')
    stock_light = pawn.get_component_by_class(ue.SpotLightComponent)
    puzzle_light = kit.get_component_by_class(ue.SpotLightComponent)
    maximum = float(prop(cell, 'MaxBatteryLevel'))
    stock_tick = float(prop(cell, 'DepletionSpeed')) + .35
    hand = lambda: str(prop(kit, 'Equipped'))
    selected_filter = lambda: str(prop(kit, 'Filter'))
    stock = lambda: float(prop(cell, 'CurrentBatteryLevel'))
    puzzle = lambda: float(prop(kit, 'PuzzleCharge'))
    welder = lambda: float(prop(kit, 'Charge'))

    def rows():
        result = []
        for slot in prop(inv, 'InventorySlots'):
            item = prop(slot, SLOT_ITEM)
            result.append((item.get_name() if item else '', int(prop(slot, SLOT_AMOUNT))))
        return result

    def item_class(kind):
        if kind == 'FlashlightBattery':
            return 'BP_InventoryItem_FlashlightBattery_C'
        return 'BP_Pack_' + kind + '_C'

    def tap(key):
        ue.SystemLibrary.execute_console_command(world, 'Input.+key ' + key, pc)
        try:
            yield 0
        finally:
            ue.SystemLibrary.execute_console_command(world, 'Input.-key ' + key, pc)
        yield .21

    def equip(kind):
        before, old = rows(), hand()
        index = next(i for i, (name, count) in enumerate(before) if name == item_class(kind) and count > 0)
        yield from tap('Tab')
        if not menu.is_visible():
            raise AssertionError('Tab did not open the stock inventory while holding ' + old)
        yield from tap(DIGITS[index])
        after = rows()
        if hand() != kind or menu.is_visible() or pc.show_mouse_cursor:
            raise AssertionError(f'Slot {index + 1} selected from Tab did not equip {kind}: {hand()}, {after}')
        expected = before.copy()
        expected[index] = ('', 0) if old == 'None' else (item_class(old), 1)
        if after != expected:
            raise AssertionError(f'Equipping {kind} changed unexpected slots: {before} -> {after}')

    def aim(point, side, distance):
        pos = point + side * distance
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(ue.Vector(pos.x, pos.y, deck_height), False, True)
        yield .16
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))
        yield .20

    def interact(kind):
        fixture = next(f for f in fixtures if str(prop(f, 'Kind')) == kind)
        center = ue.SystemLibrary.get_component_bounds(prop(fixture, 'Trigger'))[0]
        for side in (ue.Vector(0, 1, 0), ue.Vector(0, -1, 0), ue.Vector(1, 0, 0), ue.Vector(-1, 0, 0)):
            yield from aim(center, side, 98)
            if fixture.prompt(kit) and kit.get_prompt() == fixture.prompt(kit):
                yield from tap('E')
                return
        raise AssertionError(f'Could not target {kind}: {kit.get_prompt()} (expected {fixture.prompt(kit)})')

    def choose(color):
        for _ in range(5):
            if selected_filter() == color:
                return
            yield from tap('Q')
        raise AssertionError(f'Could not select {color} with Q')

    def item_art(kind):
        name = 'BP_Pack_' + kind
        cls = ue.load_class(None, f'/Game/ShipMechanics/Pack/{name}.{name}_C')
        data = prop(ue.get_default_object(cls), 'ItemData')
        icon = prop(data, ITEM_ICON)
        return str(prop(data, ITEM_NAME)), icon.get_path_name() if icon else ''

    check('Fresh showroom starts with the ordinary flashlight in hand',
          game.get_current_level_name(world) == 'L_ModelsAndMechanics_Showcase'
          and hand() == 'StockLamp' and maximum > 0 and stock() > 0 and not prop(kit, 'bHasColorLamp'))
    ordinary_art, puzzle_art = item_art('StockLamp'), item_art('ColorLamp')
    check('The two stock inventory entries have descriptive names and distinct icons',
          any(term in ordinary_art[0].lower() for term in ('zwyk', 'osobist'))
          and 'zagad' in puzzle_art[0].lower() and ordinary_art[1] and puzzle_art[1]
          and ordinary_art[1] != puzzle_art[1], [ordinary_art, puzzle_art])

    yield from interact('ColorLamp')
    check('Picking up the puzzle flashlight supplies its full built-in battery without the welder pack',
          hand() == 'StockLamp' and (item_class('ColorLamp'), 1) in rows() and puzzle() == 100
          and not prop(kit, 'bHasBattery'))
    yield from interact('White')
    check('The converted pickup provides one usable ordinary flashlight battery',
          (item_class('FlashlightBattery'), 1) in rows() and (item_class('White'), 1) not in rows(), rows())

    if stock_light.is_visible():
        yield from tap('F')
    yield from tap('F')
    before = stock()
    yield stock_tick
    check('F lights the ordinary flashlight and only its stock cell drains',
          stock_light.is_visible() and not puzzle_light.is_visible() and stock() < before and puzzle() == 100,
          {'ordinary': [before, stock()], 'puzzle': puzzle()})

    yield from equip('ColorLamp')
    check('Swapping through Tab stows the lit ordinary lamp and starts the puzzle lamp switched off',
          not stock_light.is_visible() and not puzzle_light.is_visible() and not prop(kit, 'bLampOn'))
    yield from tap('F')
    before, other, tool = puzzle(), stock(), welder()
    yield max(.8, stock_tick)
    check('F lights the puzzle flashlight and only its built-in cell drains',
          puzzle_light.is_visible() and not stock_light.is_visible() and puzzle() < before
          and stock() == other and welder() == tool, {'puzzle': [before, puzzle()], 'ordinary': [other, stock()]})
    cycle = [selected_filter()]
    for _ in range(5):
        yield from tap('Q')
        cycle.append(selected_filter())
    check('Q cycles through all five filters and wraps to the starting color',
          set(cycle[:-1]) == {'Red', 'Green', 'Blue', 'UV', 'White'} and cycle[0] == cycle[-1], cycle)

    yield from equip('StockLamp')
    stored = puzzle()
    yield from tap('F')
    before = stock()
    yield stock_tick
    check('The lit ordinary lamp drains while the charged puzzle lamp stays off in the backpack',
          stock_light.is_visible() and not puzzle_light.is_visible() and stock() < before
          and 0 < stored == puzzle(), {'ordinary': [before, stock()], 'puzzle': [stored, puzzle()]})
    yield from equip('ColorLamp')
    check('Re-equipping a charged puzzle lamp preserves its remaining power and leaves it off',
          not puzzle_light.is_visible() and not stock_light.is_visible() and puzzle() == stored)
    other = stock()
    yield from tap('F')

    module = next(a for a in game.get_all_actors_of_class(world, ue.ShipLightModule)
                  if a.actor_has_tag('LAB-MODULE-WALL-A'))
    crate, = game.get_all_actors_with_tag(world, 'LAB-MODULE-CRATE-A')
    crate_mesh = crate.get_component_by_class(ue.StaticMeshComponent)
    block = prop(module, 'Block')
    size = prop(module, 'BlockSize')
    normal = block.get_forward_vector()
    target = block.get_world_location() + normal * (size.x * .5 + 1) + block.get_up_vector() * (size.z * .35)
    yield from choose('Green')
    yield from aim(target, normal, 170)
    yield .3
    check('Puzzle green latches the nearby crate for a white-light control',
          module.has_cargo() and not crate_mesh.is_simulating_physics())
    # Shorten only the remaining lit lifetime on this PIE instance. Its normal drain must extinguish the lamp.
    kit.set_editor_property('PuzzleCharge', .05)
    yield max(.5, .05 / max(.01, kit.get_lamp_drain_per_second()) + .25)
    check('Exhausting the puzzle cell extinguishes it and preserves the latched cargo and the ordinary cell',
          puzzle() == 0 and not prop(kit, 'bLampOn') and not puzzle_light.is_visible()
          and module.has_cargo() and stock() == other)
    yield from tap('F')
    check('An empty puzzle flashlight stays off when F is pressed', not puzzle_light.is_visible() and puzzle() == 0)
    yield from equip('StockLamp')
    check('Re-equipping the ordinary lamp through Tab leaves it switched off',
          not stock_light.is_visible() and not puzzle_light.is_visible())
    yield from tap('F')
    before = stock()
    yield stock_tick
    check('A spent puzzle lamp leaves ordinary illumination available and ordinary white cannot release the crate',
          stock_light.is_visible() and stock() < before and puzzle() == 0 and module.has_cargo()
          and not crate_mesh.is_simulating_physics(), {'ordinary': [before, stock()], 'puzzle': puzzle()})

    yield from equip('FlashlightBattery')
    before, tool = stock(), welder()
    yield from tap('LeftMouseButton')
    check('Using the handheld spare consumes it and replenishes only the ordinary cell',
          hand() == 'None' and stock() > before and stock() <= maximum and puzzle() == 0 and welder() == tool
          and all(name != item_class('FlashlightBattery') for name, count in rows()),
          {'ordinary': [before, stock()], 'puzzle': puzzle(), 'welder': welder()})

    yield from interact('Welder')
    yield from interact('Battery')
    check('Collecting the separate welder battery leaves the puzzle lamp empty',
          prop(kit, 'bHasBattery') and welder() == 100 and puzzle() == 0)
    # Give every reserve distinct headroom before each use so cross-charging is observable.
    for kind, index in [('ColorLamp', 1), ('StockLamp', 0), ('Welder', 2), ('Battery', 2)]:
        yield from equip(kind)
        for _ in range(4):
            cell.call_method('DepleteBattery', ())
        kit.set_editor_property('PuzzleCharge', 28.)
        kit.set_editor_property('Charge', 46.)
        before = [stock(), puzzle(), welder()]
        has_headroom = all(0 < value < limit for value, limit in zip(before, [maximum, 100., 100.]))
        check('All three batteries have headroom before charging ' + kind, has_headroom, before)
        if not has_headroom:
            raise AssertionError(f'Could not prepare partial charges before testing {kind}: {before}')
        yield from interact('Charger')
        after = [stock(), puzzle(), welder()]
        expected = before.copy()
        expected[index] = maximum if index == 0 else 100.
        check('The charger refills only the equipped ' + kind, after == expected,
              {'before': before, 'after': after, 'expected': expected})

    yield from equip('ColorLamp')
    yield from choose('White')
    yield from aim(target, normal, 170)
    before = puzzle()
    yield .45
    check('Returning to the stored puzzle lamp preserves its charge until F',
          before == puzzle() and not puzzle_light.is_visible() and module.has_cargo())
    yield from tap('F')
    yield .3
    check('Puzzle white releases the same crate from the same aim point',
          puzzle_light.is_visible() and selected_filter() == 'White'
          and not module.has_cargo() and crate_mesh.is_simulating_physics())
