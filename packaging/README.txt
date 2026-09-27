Dynamic Banners-N-Flags v{VERSION}
for American Truck Simulator ({GAME_VERSION})
https://github.com/PicSoul/dynamic-banners-n-flags
====================================================================

Your truck's oversize banners, warning flags and beacon units (and those on
your attached trailers) are only shown while your beacons are on. Turn the
beacons off and they disappear; turn them on and they come back.

Beacon units are roof/chassis beacons and trailer beacon bars and strobe bars.
Only accessories whose 3D model actually has beacon lights are toggled, so
bumpers, doors and rear frames never disappear. (Bumpers with built-in
strobes keep their lamp housings; the strobes are simply off.)

Only YOUR truck and trailers are affected - never AI traffic or other players.


INSTALL
-------
1. Copy dynamic_banners.dll into your American Truck Simulator folder:

     ...\steamapps\common\American Truck Simulator\bin\win_x64\plugins\

   (Create the "plugins" folder if it does not exist. In Steam: right-click
   American Truck Simulator > Manage > Browse local files > bin > win_x64.)

2. Start the game. ATS asks whether to allow "SDK plugins" - accept.

3. Press ~ to open the console. You should see:
     [Dynamic Banners] v{VERSION} active - banners/flags on your truck and
     trailers follow your beacons

The plugin creates dynamic_banners.ini next to itself on first start. You can
edit it to change the behaviour (for example InvertBeacon = 1 to hide the
banners while the beacons are ON instead). Delete it to go back to defaults.


WHERE IT APPLIES
----------------
  Driving, photo mode ........ banners/flags follow your beacons
  Pause menu, service center . always shown (so you can see what's installed)

Supported accessory slots: f_banner, flag_f_l, flag_f_r (trucks) and
r_banner, flag_r_l, flag_r_r (trailers) - every base-game truck and trailer -
plus beacon units in the beacon, chs_beacon and rear_body slots. Modded
trucks/trailers that use the same slot names work too. Set HideBeacons = 0 in
dynamic_banners.ini to keep beacon units always visible.


AFTER A GAME UPDATE
-------------------
If the console says "[Dynamic Banners] INACTIVE", the game update changed
something the plugin relies on. The plugin then does nothing at all (your game
is not affected). Check the GitHub page above for an updated version.


UNINSTALL
---------
Delete dynamic_banners.dll (and dynamic_banners.ini / dynamic_banners.log)
from the bin\win_x64\plugins folder.


TRUCKERSMP / MULTIPLAYER
------------------------
The plugin changes how your own truck is drawn on your own screen, and hooks
one small game function to hide the flag cloth. TruckersMP has its own rules
about client modifications and may treat memory-modifying plugins as a
violation. USE IT ON TRUCKERSMP AT YOUR OWN RISK. SCS Convoy (the built-in
multiplayer) is fine: only your own truck and trailers are affected, and only
on your own screen.


License: MIT - see LICENSE.txt. Includes MinHook and SCS SDK headers - see
THIRD_PARTY_NOTICES.txt.
