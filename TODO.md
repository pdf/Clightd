## 1.2
- [x] modularize source code better

## DRM branch
- [x] switch to libdrm for both gamma and dpms
- [x] fix getdpms
- [x] understand how to check if connector is really active
- [ ] create drm_utils c source file?
- [ ] drop get/set dpms timeout (not supported on drm i guess) 
- [ ] remove useless dep
- [ ] remove useless arguments to interface methods
- [ ] update readme (DRM will not work for now, it only works from tty. Let's hope in the future we will be able to get it working on X and wayland...)
