## 1.2
- [x] modularize source code better

## DRM branch
- [x] switch to libdrm for both gamma and dpms
- [x] fix getdpms
- [x] understand how to check if connector is really active
- [x] drop get/set dpms timeout (not supported on drm i guess) 
- [x] remove useless dep
- [x] remove useless arguments to interface methods
- [x] better error checking in dpms and gamma
- [x] update readme (DRM will not work for now, it only works from tty. Let's hope in the future we will be able to get it working on X and wayland...)
