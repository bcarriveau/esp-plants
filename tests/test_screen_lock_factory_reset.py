"""Focused static regression checks; does not substitute firmware/hardware builds."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / 'firmware' / 'waveshare-hub'
MAIN = (HUB / 'src' / 'main.cpp').read_text(encoding='utf-8')
H2 = (ROOT / 'firmware/m5-h2-zigbee/src/main.cpp').read_text(encoding='utf-8')
PROTO = (ROOT / 'shared/plantlink/plantlink.h').read_text(encoding='utf-8')


def widgets(root):
    yield root
    for c in root.get('children', []):
        yield from widgets(c)


def by_id(root, name):
    return next(w for w in widgets(root) if w.get('identifier') == name)


def rect(w):
    return tuple(int(w[k]) for k in ('left', 'top', 'width', 'height'))


def test_both_eez_projects_have_reset_ui_and_no_offscreen_geometry():
    for suffix, width, height in [('',800,480),('_7b',1024,600)]:
        j = json.loads((HUB / f'esp_plants{suffix}.eez-project').read_text())
        assert len(j['userPages']) == 5
        a = j['userPages'][-1]['components'][0]
        advanced = by_id(a, 'advanced_card')
        button = by_id(a,'advanced_factory_reset_button')
        back = by_id(a,'advanced_back_button')
        add = by_id(a,'advanced_add_button')
        scrim = by_id(a,'factory_reset_scrim')
        modal = by_id(a,'factory_reset_dialog')
        assert rect(scrim) == (0,0,width,height)
        mx,my,mw,mh = rect(modal)
        assert mx >= 0 and my >= 0 and mx+mw <= width and my+mh <= height
        assert my+mh < (422 if not suffix else 528)
        ax,ay,aw,ah = rect(advanced)
        bx,by,bw,bh = rect(button)
        assert bx>=0 and by>=0 and bx+bw<=aw and by+bh<=ah
        bback=rect(back); badd=rect(add)
        assert bx+bw < bback[0]
        assert bback[0]+bback[2] < badd[0]
        for name in ('factory_reset_title','factory_reset_message',
                     'factory_reset_cancel_button','factory_reset_confirm_button'):
            x,y,w,h = rect(by_id(modal,name))
            assert x>=0 and y>=0 and x+w<=mw and y+h<=mh, (suffix,name)
        assert by_id(a,'factory_reset_confirm_label')['text'] == 'HOLD 5S TO RESET'
        assert by_id(a,'factory_reset_scrim').get('hiddenFlag') is True
        assert by_id(a,'factory_reset_dialog').get('hiddenFlag') is True
        # Settings is a separate page; don't add a duplicate destructive control there.
        settings=j['userPages'][3]['components'][0]
        assert not any('factory_reset' in (v.get('identifier') or '') for v in widgets(settings))


def test_source_and_eez_generated_all_page_bindings():
    for suffix, folder in [('', 'ui'),('_7b','ui_7b')]:
        project=json.loads((HUB/f'esp_plants{suffix}.eez-project').read_text())
        template=next(f['template'] for f in project['settings']['build']['files'] if f['fileName']=='ui.c')
        gen=(HUB/'src'/folder/'ui.c').read_text()
        assert gen == template
        header=(HUB/'src'/folder/'screens.h').read_text()
        screens=(HUB/'src'/folder/'screens.c').read_text()
        for name in ('factory_reset_scrim','factory_reset_dialog','factory_reset_message',
                     'factory_reset_confirm_button','factory_reset_cancel_button',
                     'advanced_factory_reset_button'):
            assert f'lv_obj_t *{name};' in header
            assert f'objects.{name} =' in screens
        assert 'simCreateLockControls();' in gen
        assert 'simOpenResetEvent' in gen
        assert 'simConfirmResetEvent' in gen
        # LVGL 8.3.11 has no LV_SYMBOL_LOCK: use an actual padlock drawn
        # from 2 LVGL objects so both simulator variants show the indicator.
        assert 'simBuildHeaderLockIndicator();' in gen
        assert 'simLockBody = lv_obj_create(objects.header);' in gen
        assert 'simLockShackle = lv_obj_create(objects.header);' in gen
        assert 'simSetHidden(simLockBody, !simLocked);' in gen
        assert 'simSetHidden(simLockShackle, !simLocked);' in gen
        assert 'LV_SYMBOL_LOCK' not in gen
        assert 'simShowPage(SIM_PAGE_HOME);' in gen


def test_lock_is_three_second_hold_persistent_and_all_page_touch_shield():
    assert 'constexpr uint32_t kScreenLockHoldMs = 3000;' in MAIN
    assert 'if (lockPressArmed && millis() - lockPressStartedMs >= kScreenLockHoldMs)' in MAIN
    assert 'preferences.getBool("screen_lock", false)' in MAIN
    assert 'preferences.putBool("screen_lock", locked)' in MAIN
    assert 'buildHeaderLockIndicator();' in MAIN
    assert 'headerLockBody = lv_obj_create(objects.header);' in MAIN
    assert 'headerLockShackle = lv_obj_create(objects.header);' in MAIN
    assert 'showHeaderLockIndicator();' in MAIN
    assert 'screenLocked ? -uiX(21) : 0' in MAIN
    assert 'screenLocked ? " " LV_SYMBOL_LOCK' not in MAIN
    assert 'lockTouchShield = lv_obj_create(lv_layer_top());' in MAIN
    assert 'lockCountHotspot = lv_obj_create(objects.header);' in MAIN
    assert 'lv_obj_add_event_cb(lockTouchShield, lockCountEvent, LV_EVENT_ALL' in MAIN
    assert 'if (lockCountHotspot) lv_obj_del(lockCountHotspot)' in MAIN
    assert 'if (lockTouchShield) lv_obj_del(lockTouchShield)' in MAIN
    assert 'servicePlantLink();' in MAIN  # still runs while locked


def test_reset_requires_hold_h2_ack_and_preserves_firmware():
    assert 'constexpr uint32_t kFactoryResetHoldMs = 5000;' in MAIN
    assert 'factoryResetHolding &&' not in MAIN or 'kFactoryResetHoldMs' in MAIN
    assert 'millis() - factoryResetHoldStartedMs < kFactoryResetHoldMs' in MAIN
    assert 'espplants_update::installing()' in MAIN
    assert 'if (!h2Online || !h2BuildId[0])' in MAIN
    assert 'FactoryResetState::WaitPrepared' in MAIN
    assert 'FactoryResetState::WaitExecuted' in MAIN
    assert 'phase == plantlink::kFactoryResetAcceptedMagic' in MAIN
    assert 'factoryResetState = FactoryResetState::Wipe;' in MAIN
    assert 'preferences.clear();' in MAIN
    assert 'network.begin("espnet", false, "plantdata")' in MAIN
    assert 'legacy.begin("espplants", false, "nvs")' in MAIN
    assert 'preferences.putUInt("_schema", 1)' in MAIN
    assert 'ESP.restart();' in MAIN
    assert 'esp_partition_erase_range' not in MAIN
    assert 'Zigbee.factoryReset(true);' in H2
    assert 'kFactoryResetArmMs = 10000' in H2
    assert 'ignored unarmed or expired factory reset execute' in H2
    assert 'rejected malformed factory reset request' in H2
    assert 'factoryResetExecuteAtMs = millis() + kFactoryResetH2DelayMs' in H2
    for token in ('kFactoryResetPayloadBytes=12','kFactoryResetPrepareMagic',
                  'kFactoryResetExecuteMagic','kFactoryResetAcceptedMagic',
                  'kFactoryResetGuardMagic'):
        assert token in PROTO


def test_unchanged_com11_and_7b_environment():
    pio=(HUB/'platformio.ini').read_text()
    base=pio.split('[env:waveshare_s3_touch_lcd_7]',1)[1].split('[env:',1)[0]
    assert 'upload_port = COM11' in base
    assert 'upload_port = COM13' in pio
    assert 'waveshare_s3_touch_lcd_7b_diag' in pio


def test_padlock_geometry_inside_both_header_sizes():
    # Body + shackle in 800x480 EEZ coordinates, scaled to the 7B 1024x600.
    for scale_x, scale_y, width, header_height in [(100, 100, 800, 66),
                                                    (128, 125, 1024, 83)]:
        def sx(x): return (x * scale_x + 50) // 100
        def sy(y): return (y * scale_y + 50) // 100
        for x,y,w,h in [(764,30,14,12),(767,22,8,14)]:
            assert 0 <= sx(x) < sx(x)+sx(w) <= width
            assert 0 <= sy(y) < sy(y)+sy(h) <= header_height
        # The existing count end margin is 22 EEZ coordinate units.
        # Translating left 21 units leaves the icon clear of the label.
        assert sx(800 - 22 - 21) < sx(764)
