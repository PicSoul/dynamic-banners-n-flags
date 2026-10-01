Dynamic Banners-N-Flags v{VERSION}
for American Truck Simulator and Euro Truck Simulator 2 ({GAME_VERSION})
https://github.com/PicSoul/dynamic-banners-n-flags
====================================================================

Your truck's oversize banners and warning flags (and those on your attached
trailers) are only shown while your beacons are on. Turn the beacons off and
they disappear; turn them on and they come back.

Optional: set HideBeacons = 1 in dynamic_banners.ini to make the beacon units
themselves (roof/chassis beacons, trailer beacon bars and strobe bars) appear
and disappear too. Only accessories whose 3D model actually has beacon lights
are toggled, so bumpers, doors and rear frames never disappear. (Bumpers and
grills with built-in strobes keep their lamp housings; the strobes are simply
off.)

Only YOUR truck and trailers are affected - never AI traffic or other players.


INSTALL
-------
1. Copy dynamic_banners.dll into the game's plugins folder:

     ATS:  ...\steamapps\common\American Truck Simulator\bin\win_x64\plugins\
     ETS2: ...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\

   (Create the "plugins" folder if it does not exist. In Steam: right-click
   the game > Manage > Browse local files > bin > win_x64.)
   Playing both games? Put a copy in each.

2. Start the game. It asks whether to allow "SDK plugins" - accept.

3. Press ~ to open the console. You should see:
     [Dynamic Banners] v{VERSION} active - banners/flags on your truck and
     trailers follow your beacons

The plugin creates dynamic_banners.ini next to itself on first start, with the
right defaults for that game. You can edit it to change the behaviour (for
example InvertBeacon = 1 to hide them while the beacons are ON instead, or
HideBeacons = 1 to toggle the beacon units too). Delete it to go back to the
defaults. After an update, new settings are added to your existing file
automatically; your values are kept.


WHAT TOGGLES
------------
  American Truck Simulator: front oversize banner and warning flags (trucks),
    rear banners and warning flags (trailers).
  Euro Truck Simulator 2: trailer rear signs (wide/long vehicle, TIR). ETS2
    trucks have no oversize banners or warning flags; their national flags
    keep showing unless you add flag_l, flag_r to Slots.
  Both games, with HideBeacons = 1: beacon units as well.

Modded trucks/trailers that use the same accessory slot names work too.


WHERE IT APPLIES
----------------
  Driving, photo mode ........ follow your beacons
  Pause menu, service center . always shown (so you can see what's installed)


AFTER A GAME UPDATE
-------------------
If the console says "[Dynamic Banners] INACTIVE", the game update changed
something the plugin relies on. The plugin then does nothing at all (your game
is not affected). Check the GitHub page above for an updated version.


UNINSTALL
---------
Delete dynamic_banners.dll (and dynamic_banners.ini / dynamic_banners.log)
from the game's bin\win_x64\plugins folder.


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
