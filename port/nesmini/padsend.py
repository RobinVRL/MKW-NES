#!/usr/bin/env python3
"""Play on the NES Mini with a controller connected to the PC (PS5, Xbox, ...).

Reads the controller with pygame and streams it over hakchi's USB link to
padbridge on the console, which shows up there as a normal gamepad.
  python padsend.py            pad only (start the game afterwards)
  python padsend.py --launch   pad, then start the game from D: (/media/pc/mkw)
The game only looks for controllers when it starts, so the pad has to be
running first. Ctrl+C to stop.

PS5 layout: Cross = A (accelerate), Circle = B, Square = Y, Triangle = X,
L1/R1 = L/R, L2/R2 = ZL/ZR, Options = Start, Create = Select,
left stick / d-pad = steer, PS button = quit the game.
"""
import argparse, os, struct, subprocess, sys, time
os.environ['SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS'] = '1'
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = '1'
import pygame
from pygame._sdl2 import controller as gc

ap = argparse.ArgumentParser()
ap.add_argument('--host', default='169.254.13.37')
ap.add_argument('--dir', default='/media/pc/mkw', help='folder on the console with padbridge and the game')
ap.add_argument('--launch', action='store_true', help='start the game once the pad is ready')
a = ap.parse_args()

pygame.display.init()
gc.init()
pad = None
while not pad:
    pygame.event.pump()
    for i in range(gc.get_count()):
        if gc.is_controller(i):
            pad = gc.Controller(i)
            break
    if not pad:
        print('waiting for a controller (press the PS button or plug it in)...', end='\r', flush=True)
        time.sleep(0.5)
print('controller:', pad.name, flush=True)

# padbridge bit order: SOUTH EAST NORTH WEST TL TR TL2 TR2 SELECT START MODE.
# The game reads south as A (like the NES Classic pad), so Cross -> A, Circle -> B.
B = pygame
BUTTONS = [
    (B.CONTROLLER_BUTTON_A, 0),        # Cross    -> south -> A
    (B.CONTROLLER_BUTTON_B, 1),        # Circle   -> east  -> B
    (B.CONTROLLER_BUTTON_Y, 2),        # Triangle -> X
    (B.CONTROLLER_BUTTON_X, 3),        # Square   -> Y
    (B.CONTROLLER_BUTTON_LEFTSHOULDER, 4),
    (B.CONTROLLER_BUTTON_RIGHTSHOULDER, 5),
    (B.CONTROLLER_BUTTON_BACK, 8),     # Create
    (B.CONTROLLER_BUTTON_START, 9),    # Options
    (B.CONTROLLER_BUTTON_GUIDE, 10),   # PS button
]

# The game is detached (nohup, own output) so losing the PC link doesn't kill
# it half-started; the pad then just stops responding.
game = (f'(sleep 1; MKW_SDMC={a.dir}/sdmc nohup sh {a.dir}/mkw.sh </dev/null >/dev/null 2>&1) & '
        if a.launch else '')
# The D: mount doesn't survive a console reboot: mount it again if needed,
# using this PC's address on the link to the console.
import socket
_s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); _s.connect((a.host, 22)); pc_ip = _s.getsockname()[0]; _s.close()
mount = (f'[ -x {a.dir}/padbridge ] || {{ modprobe cifs; sleep 1; mkdir -p /media/pc; '
         f'mount -t cifs //{pc_ip}/D /media/pc -o username=nesmini,password=Nes12345,sec=ntlmssp,'
         f'uid=0,gid=0,file_mode=0777,dir_mode=0777; }}; ')
remote = f'{mount}{game}exec {a.dir}/padbridge'
ssh = subprocess.Popen(['ssh', '-o', 'StrictHostKeyChecking=no', f'root@{a.host}', remote],
                       stdin=subprocess.PIPE, bufsize=0)
print('sending input... Ctrl+C to stop')
last, last_t = None, 0.0
try:
    while ssh.poll() is None:
        pygame.event.pump()
        btn = 0
        for b, bit in BUTTONS:
            if pad.get_button(b):
                btn |= 1 << bit
        if pad.get_axis(B.CONTROLLER_AXIS_TRIGGERLEFT) > 12000: btn |= 1 << 6
        if pad.get_axis(B.CONTROLLER_AXIS_TRIGGERRIGHT) > 12000: btn |= 1 << 7
        x = pad.get_axis(B.CONTROLLER_AXIS_LEFTX)
        y = pad.get_axis(B.CONTROLLER_AXIS_LEFTY)
        hx = pad.get_button(B.CONTROLLER_BUTTON_DPAD_RIGHT) - pad.get_button(B.CONTROLLER_BUTTON_DPAD_LEFT)
        hy = pad.get_button(B.CONTROLLER_BUTTON_DPAD_DOWN) - pad.get_button(B.CONTROLLER_BUTTON_DPAD_UP)
        pkt = struct.pack('<HhhbB', btn, x, y, hx, hy & 0xff)
        now = time.time()
        # Send on change, plus a keep-alive every 0.5 s.
        if pkt != last or now - last_t > 0.5:
            ssh.stdin.write(pkt)
            last, last_t = pkt, now
        time.sleep(1 / 250)
except (KeyboardInterrupt, BrokenPipeError, OSError):
    pass
finally:
    try: ssh.stdin.close()
    except OSError: pass
    ssh.wait(timeout=5)
