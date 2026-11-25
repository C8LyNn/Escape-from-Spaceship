"""Exercise actual player input and count rendered ink pixels in the showroom PIE world."""
import unreal as ue


def cases(world, check):
    game = ue.GameplayStatics
    pc = game.get_player_controller(world, 0)
    pawn = game.get_player_pawn(world, 0)
    camera = pawn.get_component_by_class(ue.CameraComponent)
    kit = game.get_actor_of_class(world, ue.ShipSurvivor)
    fixtures = game.get_all_actors_of_class(world, ue.ShipFixture)
    prop = lambda obj, name: obj.get_editor_property(name)
    floor_z = pawn.get_actor_location().z

    def key(name, down):
        ue.SystemLibrary.execute_console_command(world, 'Input.' + ('+' if down else '-') + 'key ' + name, pc)

    def tap(name):
        key(name, True)
        yield 0
        key(name, False)
        yield .25

    def aim(point, side, distance):
        pos = point + side * distance
        pawn.character_movement.stop_movement_immediately()
        pawn.set_actor_location(ue.Vector(pos.x, pos.y, floor_z), False, True)
        yield .2
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), point))
        yield .3

    def find(kind):
        return next(a for a in fixtures if str(prop(a, 'Kind')) == kind)

    ladder = find('Ladder')
    yield from aim(ladder.get_actor_location() + ue.Vector(0, 0, 115), ladder.get_actor_forward_vector(), 105)
    yield from tap('E')
    check('ladder keeps current equipment HUD', kit.is_on_ladder() and not prop(kit, 'bKitEnabled'))
    yield from tap('E')
    lock_cls = ue.load_class(None, '/Game/Blueprints/Actors/Locks/BP_Lock.BP_Lock_C')
    lock = game.get_actor_of_class(world, lock_cls)
    lock_center = ue.SystemLibrary.get_component_bounds(prop(lock, 'Lock'))[0]
    for side in (lock.get_actor_forward_vector(), -lock.get_actor_forward_vector(), lock.get_actor_right_vector()):
        yield from aim(lock_center, side, 99)
        if pawn.call_method('LineTrace', (350.,)) == lock:
            break
    check('stock lock offers E', pawn.call_method('LineTrace', (350.,)) == lock and kit.get_prompt().startswith('E'))

    crate = find('Crate')
    yield from aim(ue.SystemLibrary.get_component_bounds(prop(crate, 'Mesh'))[0], ue.Vector(0,1,0), 115)
    key('E', True)
    yield .25
    handle = kit.get_component_by_class(ue.PhysicsHandleComponent)
    check('E grabs the targeted crate', handle.get_grabbed_component() == prop(crate, 'Mesh'))
    key('E', False)
    yield .2
    check('releasing E drops the crate', not handle.get_grabbed_component())

    for kind in ('Battery', 'ColorLamp'):
        point = ue.SystemLibrary.get_component_bounds(prop(find(kind), 'Mesh'))[0]
        yield from aim(point, ue.Vector(0, 1, 0), 100)
        yield from tap('E')
    bar_class = ue.load_class(None, '/Game/UMG/W_ProgressBars.W_ProgressBars_C')
    bars, = ue.WidgetLibrary.get_all_widgets_of_class(world, bar_class, False)
    battery_bar = next(w for w in ue.ObjectIterator(ue.ProgressBar)
                       if w.get_name() == 'BatteryBar' and w.get_outer().get_outer() == bars)
    def tint():
        c = battery_bar.get_editor_property('fill_color_and_opacity')
        return (c.r, c.g, c.b)
    palette = {'Red': (1, .016, .008), 'Green': (.025, 1, .055),
               'Blue': (.025, .12, 1), 'UV': (.43, .035, 1)}
    def check_filter(label, color):
        value = tint()
        check(label, str(prop(kit, 'Filter')) == color and
              all(abs(a-b) < .015 for a,b in zip(value, palette[color])), value)
    yield from tap('Two')  # Take the collected color lamp out of its backpack slot.
    starting_charge = kit.charge
    kit.set_editor_property('Charge', 63.5)
    check('bottom battery bar reads the equipped shared cell',
          abs(bars.call_method('Get_BatteryBar_Percent') - .635) < .001)
    yield from tap('G')
    check('stock battery readout returns after holstering the kit',
          abs(bars.call_method('Get_BatteryBar_Percent') - .635) > .01)
    check('holstering restores the neutral lower battery bar', min(tint()) > .85, tint())
    yield from tap('G')
    kit.set_editor_property('Charge', starting_charge)
    check_filter('equipping the color flashlight tints the lower bar', 'Red')
    yield from tap('MouseScrollUp')
    check_filter('wheel forward selects green on the light and lower bar', 'Green')
    yield from tap('MouseScrollDown')
    check_filter('wheel back selects red on the light and lower bar', 'Red')
    kit.set_editor_property('bLampOn', False)
    yield .15
    check_filter('turning the light off preserves selected battery tint', 'Red')
    # Set a known emission state for the render comparison.
    kit.set_editor_property('bLampOn', True)
    yield .15
    for _ in range(4):
        yield from tap('C')
    glyph = next(f for f in fixtures if f.actor_has_tag('LAB-GLYPH-RED'))
    text = prop(glyph, 'GlyphText')
    center, ext, _ = ue.SystemLibrary.get_component_bounds(text)
    normal = text.get_forward_vector()
    along = text.get_right_vector()
    width = 2 * (abs(along.x)*ext.x + abs(along.y)*ext.y + abs(along.z)*ext.z)
    yield from aim(center, normal, 85)
    pose = ue.Transform(location=center + normal * 150, rotation=ue.MathLibrary.find_look_at_rotation(center+normal, center))
    factory = ue.get_default_object(ue.GameplayStatics)
    scale = ue.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT
    capture = factory.call_method('BeginDeferredActorSpawnFromClass', (world, ue.SceneCapture2D, pose, ue.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, scale))
    factory.call_method('FinishSpawningActor', (capture, pose, scale))
    cap = capture.capture_component2d
    cap.projection_type = ue.CameraProjectionMode.ORTHOGRAPHIC
    cap.ortho_width = width * 1.15
    cap.primitive_render_mode = ue.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
    cap.show_only_component(text)
    cap.capture_source = ue.SceneCaptureSource.SCS_SCENE_COLOR_HDR
    cap.capture_every_frame = False
    flags = []
    for name in ('Atmosphere','Fog','VolumetricFog','SkyLighting','Cloud','Bloom','PostProcessing','TemporalAA'):
        flag = ue.EngineShowFlagsSetting()
        flag.set_editor_property('show_flag_name',name)
        flag.set_editor_property('enabled',False)
        flags.append(flag)
    cap.set_editor_property('show_flag_settings', flags)
    rt = ue.RenderingLibrary.create_render_target2d(world, 256, 96, ue.TextureRenderTargetFormat.RTF_RGBA16F)
    cap.texture_target = rt

    def pixels():
        cap.capture_scene()
        values = ue.RenderingLibrary.read_render_target_raw(world, rt, False)
        # The red-ink stand has red emissive lettering and paint against the cleared capture target.
        def red(v):
            return v.r > .035 and v.r > 1.8*(max(v.g,v.b)+.005)
        return [sum(red(v) for i,v in enumerate(values) if (i % 256 < 128) == left) for left in (True, False)]

    try:
        def optics():
            return {name:str(prop(kit,name)) for name in ('bHasBattery','bHasColorLamp','bLampOn','RadiusLevel','PowerLevel','bDead','bEscaped','Filter','Charge')}
        check('tested red ink with a powered narrow lamp', prop(kit,'bLampOn') and str(prop(kit,'Filter'))=='Red' and prop(kit,'RadiusLevel')==1, optics())
        pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), center))
        for _ in range(4):
            yield from tap('V')
        wide = pixels()
        check('wide beam reveals strokes on both sides', min(wide)>20, wide)
        for _ in range(4):
            yield from tap('C')
        captures = []
        for offset in (-.25, .25):
            pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), center + along * (offset*width)))
            yield .45
            captures.append(pixels())
        (a,b),(c,d) = captures
        check('moving a narrow beam moves the visible letters between halves',
              max(a,b)>20 and max(c,d)>20 and ((a>3*b and d>3*c) or (b>3*a and c>3*d)), captures)
        paint = prop(glyph, 'PaintLayer')
        paint_center, paint_ext, _ = ue.SystemLibrary.get_component_bounds(paint)
        paint_width = 2*(abs(along.x)*paint_ext.x + abs(along.y)*paint_ext.y)
        cap.clear_show_only_components()
        cap.show_only_component(paint)
        capture.set_actor_location(paint_center + normal*150, False, True)
        cap.ortho_width = paint_width*1.15
        yield from aim(paint_center, normal, 45)
        # Track a point inside a brush stroke as the narrowest beam moves off it.
        def paint_frame():
            cap.capture_scene()
            return ue.RenderingLibrary.read_render_target_raw(world, rt, False)

        kit.set_editor_property('bLampOn', False)
        yield .15
        unlit = paint_frame()
        kit.set_editor_property('bLampOn', True)
        yield .15
        lit = paint_frame()
        candidates = [i for i,(a,b) in enumerate(zip(lit,unlit)) if a.r - b.r > .25]
        if not candidates:
            raise AssertionError('Missing illuminated reference stroke')
        pixel = min(candidates, key=lambda i: (i%256-127.5)**2 + (i//256-47.5)**2)
        spacing = cap.ortho_width / 256
        sample_point = (paint_center + capture.get_actor_right_vector()*((pixel%256-127.5)*spacing)
                        + capture.get_actor_up_vector()*((47.5-pixel//256)*spacing))
        sight = ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), sample_point)
        angle = kit.get_component_by_class(ue.SpotLightComponent).outer_cone_angle
        levels = []
        for fraction in (0., .55, .80, 1.15):
            pc.set_control_rotation(ue.Rotator(roll=0, pitch=sight.pitch, yaw=sight.yaw+angle*fraction))
            yield .20
            levels.append(paint_frame()[pixel].r - unlit[pixel].r)
        profile = [v/max(.001,levels[0]) for v in levels]
        check('ink fades through a broad penumbra before reaching zero',
              levels[0] > .2 and .18 < profile[1] < .78 and .005 < profile[2] < .40
              and profile[1] > profile[2] and abs(profile[3]) < .04, profile)
        paint_pixels = []
        for offset in (-.32,.32):
            pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(), paint_center + along*(offset*paint_width)))
            yield .3
            paint_pixels.append(pixels())
        (a,b),(c,d) = paint_pixels
        check('colored paint is clipped at the beam edge', max(a,b)>10 and max(c,d)>10 and ((a>2*b and d>2*c) or (b>2*a and c>2*d)), paint_pixels)

        cap.clear_show_only_components()
        cap.show_only_component(text)
        capture.set_actor_location(center+normal*150,False,True)
        cap.ortho_width = width*1.15
        yield from aim(center,normal,85)
        yield from tap('Q')
        yield .25
        wrong = pixels()
        check('wrong filter leaves no visible red lettering', sum(wrong)==0, wrong)
        # Compare each remaining layer to its own dark reference from the same capture position.
        for color in ('Green', 'Blue', 'UV'):
            check_filter('Q selects ' + color + ' on the lower battery bar', color)
            layer = next(f for f in fixtures if f.actor_has_tag('LAB-GLYPH-' + color.upper()))
            component = prop(layer, 'GlyphText')
            point, bounds, _ = ue.SystemLibrary.get_component_bounds(component)
            span = 2 * (abs(along.x)*bounds.x + abs(along.y)*bounds.y + abs(along.z)*bounds.z)
            cap.clear_show_only_components()
            cap.show_only_component(component)
            capture.set_actor_location(point + normal*150, False, True)
            cap.ortho_width = span*1.15
            yield from aim(point, normal, 85)
            kit.set_editor_property('bLampOn', False)
            yield .15
            cap.capture_scene()
            background = ue.RenderingLibrary.read_render_target_raw(world, rt, False)
            kit.set_editor_property('bLampOn', True)
            samples = []
            for offset in (-.25, .25):
                pc.set_control_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_world_location(),point + along*(offset*span)))
                yield .3
                cap.capture_scene()
                on = ue.RenderingLibrary.read_render_target_raw(world, rt, False)
                half = [0,0]
                for i,(v,ref) in enumerate(zip(on,background)):
                    if max(v.r-ref.r, v.g-ref.g, v.b-ref.b) > .08:
                        half[int(i % 256 >= 128)] += 1
                samples.append(half)
            (a,b),(c,d) = samples
            check(color + ' reveals only the illuminated side', str(prop(kit,'Filter')) == color
                  and max(a,b)>20 and max(c,d)>20 and ((a>3*b and d>3*c) or (b>3*a and c>3*d)), samples)
            yield from tap('Q')
        cap.clear_show_only_components()
        cap.show_only_component(text)
        capture.set_actor_location(center+normal*150,False,True)
        cap.ortho_width = width*1.15
        yield from aim(center,normal,85)
        kit.set_editor_property('bLampOn', False)
        yield .25
        dark = pixels()
        check('lamp off leaves no visible lettering', sum(dark)==0, dark)
    finally:
        capture.destroy_actor()
