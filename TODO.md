## 4.0

### Generic
- [x] Fix issues with timerfd_settime for long timeouts
- [x] Add issue template
- [x] Update to new ddcutil 0.9.5 interface. Require it.
- [x] Bump to 4.0: api break
- [x] Check polkit script!
- [x] Add support for libmodule 5.0.0 api
- [x] Add a "-v/--version" cmdline switch
- [x] Require libmodule >= 5.0.0
- [ ] Change libmodule required version in WIKI pages (Build requirements)

### Idle
- [x] Avoid depending on X
- [x] IDLE support on wayland
- [x] Update API doc

### Dpms
- [x] Drop DPMS Set/getTimeouts, and only use get/set 
- [x] Add support for dpms on tty
- [x] Update doc

### CPack
- [x] Add Cpack support to cmake
- [x] Fix cpack Dependencies on ubuntu
- [x] Fix cpack: only add enabled dependencies
- [x] Fix rpm cpack -> fix cmakelists CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION

### Camera
- [x] Improve camera.c code
- [x] Reduce camera.c logging
- [x] Support Grayscale pixelformat for CAMERA sensor
- [x] Fix #24
- [ ] Improve camera brightness compute with a new histogram-based algorithm (#25)
- [x] Add a new Capture parameter to specify camera settings
- [ ] Document new capture parameter

### Backlight
- [x] Support DDCutil DDCA_Display_Info path.io_mode (eg: /dev/i2c-%d) as uid (as SerialNumber can be null/empty on some devices) (Fix #27)

### Screen
- [x] Add a method (X only) to compute current monitor emitted brightness
- [x] Update wiki pages: drop DENABLE_IDLE and add DENABLE_SCREEN
- [x] Document new Screen API
- [x] It needs xauthority too!
- [x] Document it

## 4.1

#### Gamma
- [ ] Add gamma support on wayland (??)
https://github.com/swaywm/wlroots/blob/master/examples/gamma-control.c

#### Dpms
- [ ] Add support for dpms on wayland(??)

## 4.X
- [ ] Keep it up to date with possible ddcutil/libmodule api changes

## Ideas
- [ ] follow ddcci kernel driver and in case, drop ddcutil and add the kernel driver as clightd opt-dep
