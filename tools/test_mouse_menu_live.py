"""Verify mouse menus through a disabled private game HWND and renderer readback.

No global keyboard/mouse injection, cursor movement, or focus activation.
"""
import ctypes
from ctypes import wintypes
import datetime
import json
import re
import shutil

from test_graphics_settings_live import ROOT, USER, window_for
from test_keyboard_menu_live import KeyboardRun, fingerprints, ACTION

USER.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
USER.GetClientRect.restype = wintypes.BOOL
USER.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
USER.ClientToScreen.restype = wintypes.BOOL
INITIAL = "5 0 0 0 120 0 3 0 0 100 1 1 1\n"
MENU_EVENT = re.compile(r"\[NATIVE MENU MOUSE\].* event=(\d+)")


def compact(receipt):
    return {key:value for key,value in receipt.items() if key != "log"}


def require_event(receipt, event):
    assert receipt["menu_events"] == [event], compact(receipt)


class MouseRun(KeyboardRun):
    def pointer(self, name, x, y, button=None, settle=.7, wheel=0):
        self.flush()
        hwnd = window_for(self.process.pid)
        bounds = wintypes.RECT()
        assert hwnd and USER.GetClientRect(hwnd, ctypes.byref(bounds))
        # Test coordinates name the visible 1280x720 menu; the real HWND can
        # be larger after a live window-size change.
        px, py = round(x*bounds.right/1280), round(y*bounds.bottom/720)
        packed = (px & 65535) | ((py & 65535) << 16)
        before = self.log_path.read_text(errors="replace")
        foreground = int(USER.GetForegroundWindow() or 0)
        USER.SendMessageW(hwnd, 0x0007, 0, 0)
        USER.SendMessageW(hwnd, 0x0200, 0, packed)
        if button:
            down, up, mask = (0x0201,0x0202,1) if button == "left" else (0x0204,0x0205,2)
            USER.SendMessageW(hwnd, down, mask, packed)
            USER.SendMessageW(hwnd, up, 0, packed)
        if wheel:
            point = wintypes.POINT(px,py)
            assert USER.ClientToScreen(hwnd,ctypes.byref(point))
            screen_position = (point.x & 65535) | ((point.y & 65535) << 16)
            USER.SendMessageW(hwnd,0x020A,(wheel & 65535) << 16,screen_position)
        self.wait(settle)
        self.flush()
        after = self.log_path.read_text(errors="replace")
        assert after.startswith(before)
        segment = after[len(before):]
        receipt = dict(name=name, authored_coordinates=[x,y], client_coordinates=[px,py],
            client_extent=[bounds.right,bounds.bottom], button=button, wheel=wheel,target_pid=self.process.pid,
            target_hwnd=int(hwnd), foreground_unchanged=foreground==int(USER.GetForegroundWindow() or 0),
            actions=[dict(action=int(m[1]), row=int(m[2]), direction=int(m[3]),
                          before=int(m[4]), after=int(m[5])) for m in ACTION.finditer(segment)],
            menu_events=[int(m[1]) for m in MENU_EVENT.finditer(segment)], log=segment)
        assert receipt["foreground_unchanged"], receipt
        self.receipts.append(receipt)
        (self.folder/"mouse-receipts.json").write_text(json.dumps(self.receipts,indent=2)+"\n")
        print("MOUSE",name,receipt["actions"],receipt["menu_events"],flush=True)
        return receipt


def main():
    config=json.loads((ROOT/"config/startup_replay.json").read_text())
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%SZ-%f")
    folder=ROOT/"build/mouse-fov-fix-20261004"/("live-"+stamp)
    folder.mkdir(parents=True)
    before=fingerprints(config)
    (folder/"primary-before.json").write_text(json.dumps(before,indent=2)+"\n")
    shutil.copytree(ROOT/config["profile_store"],folder/"profile")
    shutil.copytree(ROOT/config["content_store"],folder/"content")
    prefs=folder/"profile.video.cfg";prefs.write_text(INITIAL)
    lab=folder/"lab";lab.mkdir();run=MouseRun(lab,config)
    passed=False;failure=None
    try:
        run.start();run.capture("scene-original",[1280,720],True)
        run.hold("escape-pause",0x1B,.1,2)
        # The direct first-mission route has no frontend successor. Exercise
        # the original confirmation and return without quitting that route.
        require_event(run.pointer("click-exit-game-popup",640,450,"left",1.5),6)
        run.capture("quit-popup-mouse",[1280,720])
        require_event(run.pointer("popup-right-back",640,300,"right",1),7)
        run.capture("pause-after-popup-back",[1280,720])
        require_event(run.pointer("reopen-quit-popup",640,450,"left",1.5),6)
        require_event(run.pointer("click-popup-no",440,426,"left",1),7)
        run.capture("pause-after-popup-no",[1280,720])
        run.pointer("hover-options",640,380)
        run.capture("pause-hover-options",[1280,720])
        require_event(run.pointer("wheel-pause-down-one",640,380,wheel=-120),5)
        run.capture("pause-wheel-exit",[1280,720])
        require_event(run.pointer("wheel-pause-down-two",640,380,wheel=-120),5)
        run.capture("pause-wheel-resume",[1280,720])
        require_event(run.pointer("click-options",640,380,"left",1.5),6)
        run.capture("options-mouse",[1280,720])
        require_event(run.pointer("click-video",640,270,"left",2),6)
        run.capture("video-mouse",[1280,720])
        assert "[NATIVE VIDEO MENU] opened" in run.log_path.read_text(errors="replace")
        change=run.pointer("click-render-resolution",700,236,"left")
        assert len(change["actions"])==1 and change["actions"][0]["row"]==1,compact(change)
        decrease=run.pointer("wheel-render-down",700,236,wheel=-120)
        assert len(decrease["actions"])==1 and decrease["actions"][0]["row"]==1 and decrease["actions"][0]["after"]==0,compact(decrease)
        increase=run.pointer("wheel-render-up",700,236,wheel=120)
        assert len(increase["actions"])==1 and increase["actions"][0]["row"]==1 and increase["actions"][0]["after"]==1,compact(increase)
        run.capture("video-mouse-render-change",[1280,720])
        cancel=run.pointer("cancel-footer",315,645,"left",1)
        assert cancel["menu_events"]==[7],compact(cancel)
        assert prefs.read_text()==INITIAL,"Mouse Cancel persisted preview settings"
        run.pointer("reopen-video",640,270,"left",1.5)
        window_change=run.pointer("click-window-size",700,268,"left",1.5)
        assert len(window_change["actions"])==1 and window_change["actions"][0]["row"]==2,compact(window_change)
        run.capture("video-larger-window",[1280,720])
        fov=run.pointer("click-fov-110",580,455,"left")
        assert len(fov["actions"])==1 and fov["actions"][0]["row"]==8 and fov["actions"][0]["direction"]==-1,compact(fov)
        run.capture("video-mouse-fov",[1280,720])
        require_event(run.pointer("accept-footer",975,645,"left",1),6)
        assert prefs.read_text().split()[8]=="110","Mouse Accept did not save changed FOV"
        require_event(run.pointer("right-back-to-pause",640,270,"right",1),7)
        require_event(run.pointer("click-resume",640,310,"left",2),6)
        run.capture("scene-mouse-fov",[1280,720],True)
        run.hold("escape-reentered-pause",0x1B,.1,2)
        run.capture("pause-reentered",[1280,720])
        require_event(run.pointer("back-reentered-pause",640,300,"right",2),7)
        run.capture("scene-after-reentered-pause",[1280,720],True)
        passed=True
    except Exception as error:
        failure=repr(error)
        raise
    finally:
        try:run.close()
        finally:
            after=fingerprints(config)
            shutdown=json.loads((lab/"shutdown.json").read_text()) if (lab/"shutdown.json").exists() else {}
            (folder/"verification.json").write_text(json.dumps(dict(passed=passed and before==after and shutdown.get("normal",False),
                failure=failure, primary_stores_unchanged=before==after, primary_stores_used=False,
                desktop_input_injected=False, foreground_activation_api_called=False,cursor_api_called=False,
                normal_shutdown=shutdown.get("normal",False),input_route="owned-disabled-HWND SendMessageW"),indent=2)+"\n")
            assert before==after,"Primary stores changed during isolated mouse test"
    print("PASS",folder,flush=True)


if __name__=="__main__":main()
