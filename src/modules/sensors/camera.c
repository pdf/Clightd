#include <sys/mman.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <sensor.h>

#define CAMERA_NAME                 "Camera"
#define CAMERA_ILL_MAX              255
#define CAMERA_SUBSYSTEM            "video4linux"

#define SET_V4L2(id, val)           set_v4l2_control(id, val, #id)
#define SET_V4L2_DEF(id)            set_v4l2_control_def(id, #id)
#ifndef NDEBUG
    #define INFO(fmt, ...)          printf(fmt, ##__VA_ARGS__);
#else
    #define INFO(fmt, ...)
#endif

#define TEST_RET(fn) fn; if (state.quit) break;

static int recv_frames(const char *interface);
static void open_device(const char *interface);
static void set_v4l2_control_def(uint32_t id, const char *name);
static void set_v4l2_control(uint32_t id, int32_t val, const char *name);
static void set_camera_settings_def(void);
static void set_camera_settings(void);
static void init(void);
static void init_mmap(void);
static int xioctl(int request, void *arg, bool exit_on_error);
static void start_stream(void);
static void stop_stream(void);
static void send_frame(void);
static void recv_frame(int i);
static double compute_brightness(const unsigned int size);
static void free_all();

struct buffer {
    uint8_t *start;
    size_t length;
};

struct state {
    int quit;
    int width;
    int height;
    int device_fd;
    int num_captures;
    uint32_t pixelformat;
    double *brightness;
    struct buffer buf;
    char *settings;
};

static struct state state;

SENSOR(CAMERA_NAME, CAMERA_SUBSYSTEM, NULL);

/*
 * Frame capturing method
 */
static int capture(struct udev_device *dev, double *pct, const int num_captures, char *settings) {
    state.num_captures = num_captures;
    state.brightness = pct;
    state.settings = settings;
    int r = recv_frames(udev_device_get_devnode(dev));
    free_all();
    return -r;
}

static int recv_frames(const char *interface) {
    open_device(interface);
    while (!state.quit) {
        TEST_RET(init());
        TEST_RET(init_mmap());
        TEST_RET(start_stream());
        set_camera_settings();
        for (int i = 0; i < state.num_captures && !state.quit; i++) {
            send_frame();
            recv_frame(i);
        }
        stop_stream();
        break;
    }
    return state.quit;
}

static void open_device(const char *interface) {
    state.device_fd = open(interface, O_RDWR);
    if (state.device_fd == -1) {
        perror(interface);
        state.quit = errno;
    }
}

static void set_v4l2_control_def(uint32_t id, const char *name) {
    struct v4l2_queryctrl arg = {0};
    arg.id = id;
    if (-1 == xioctl(VIDIOC_QUERYCTRL, &arg, false)) {
        INFO("%s unsupported\n", name);
    } else {
        INFO("%s (%u) default val: %d\n", name, id, arg.default_value);
        set_v4l2_control(id, arg.default_value, name);
    }
}

static void set_v4l2_control(uint32_t id, int32_t val, const char *name) {
    struct v4l2_control ctrl ={0};
    ctrl.id = id;
    ctrl.value = val;
    if (-1 == xioctl(VIDIOC_S_CTRL, &ctrl, false)) {
        INFO("%s unsupported\n", name);
    } else {
        INFO("Set %u val: %d\n", id, val);
    }
}

/* Properly set everything to default value */
static void set_camera_settings_def(void) {
    SET_V4L2_DEF(V4L2_CID_SCENE_MODE);
    SET_V4L2_DEF(V4L2_CID_AUTO_WHITE_BALANCE);
    SET_V4L2_DEF(V4L2_CID_EXPOSURE_AUTO);
    SET_V4L2_DEF(V4L2_CID_AUTOGAIN);
    SET_V4L2_DEF(V4L2_CID_ISO_SENSITIVITY_AUTO);
    SET_V4L2_DEF(V4L2_CID_BACKLIGHT_COMPENSATION);
    SET_V4L2_DEF(V4L2_CID_AUTOBRIGHTNESS);
    
    SET_V4L2_DEF(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
    SET_V4L2_DEF(V4L2_CID_EXPOSURE_ABSOLUTE);
    SET_V4L2_DEF(V4L2_CID_IRIS_ABSOLUTE);
    SET_V4L2_DEF(V4L2_CID_GAIN);
    SET_V4L2_DEF(V4L2_CID_ISO_SENSITIVITY);
    SET_V4L2_DEF(V4L2_CID_BRIGHTNESS);
}

/* Parse settings string! */
static void set_camera_settings(void) {
    /* Set default values */
    set_camera_settings_def();
    if (state.settings && strlen(state.settings)) {
        char *token; 
        char *rest = state.settings; 
        
        while ((token = strtok_r(rest, ",", &rest))) {
            uint32_t v4l2_op;
            int32_t v4l2_val;
            if (sscanf(token, "%u=%d", &v4l2_op, &v4l2_val) == 2) {
                SET_V4L2(v4l2_op, v4l2_val);
            }
        }
    }
}

static void init(void) {
    struct v4l2_capability caps = {{0}};
    if (-1 == xioctl(VIDIOC_QUERYCAP, &caps, true)) {
        perror("Querying Capabilities");
        return;
    }
    
    // check if it is a capture dev
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        perror("No video capture device");
        state.quit = EINVAL;
        return;
    }
    
    // check if it does support streaming
    if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
        perror("Device does not support streaming i/o");
        state.quit = EINVAL;
        return;
    }
    
    // check device priority level. No need to quit if this is not supported.
    enum v4l2_priority priority = V4L2_PRIORITY_BACKGROUND;
    if (-1 == xioctl(VIDIOC_S_PRIORITY, &priority, false)) {
        INFO("Failed to set priority\n");
    }
    
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 160;
    fmt.fmt.pix.height = 120;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    
    /* Check supported formats */
    struct v4l2_fmtdesc fmtdesc = {0};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (xioctl(VIDIOC_ENUM_FMT, &fmtdesc, false) == 0 && fmt.fmt.pix.pixelformat == 0) {    
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_GREY) {
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
        } else if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV) {
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        }
        fmtdesc.index++;
    }
    
    if (fmt.fmt.pix.pixelformat == 0) {
        perror("Device does not support neither GREY nor YUYV pixelformats.");
        state.quit = EINVAL;
        return;
    }
    
    INFO("Using %s pixelformat.\n", (char *)&fmt.fmt.pix.pixelformat);
    
    if (-1 == xioctl(VIDIOC_S_FMT, &fmt, true)) {
        perror("Setting Pixel Format");
        return;
    }    
        
    state.width = fmt.fmt.pix.width;
    state.height = fmt.fmt.pix.height;
    state.pixelformat = fmt.fmt.pix.pixelformat;
}

static void init_mmap(void) {
    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (-1 == xioctl(VIDIOC_REQBUFS, &req, true)) {
        perror("Requesting Buffer");
        return;
    }
    
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(VIDIOC_QUERYBUF, &buf, true)) {
        perror("Querying Buffer");
        return;
    }
        
    state.buf.start = mmap(NULL,
                            buf.length,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            state.device_fd, buf.m.offset);
        
    if (MAP_FAILED == state.buf.start) {
        perror("mmap");
        state.quit = errno;
    } else {
        state.buf.length = buf.length;
    }
}

static int xioctl(int request, void *arg, bool exit_on_error) {
    int r;
    
    do {
        r = ioctl(state.device_fd, request, arg);
    } while (-1 == r && EINTR == errno);
    
    if (r == -1 && exit_on_error) {
        state.quit = errno;
    }
    return r;
}

static void start_stream(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(VIDIOC_STREAMON, &type, true)) {
        perror("Start Capture");
    }
}

static void stop_stream(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 == xioctl(VIDIOC_STREAMOFF, &type, true)) {
        perror("Stop Capture");
    }
}

static void send_frame(void) {
    struct v4l2_buffer buf = {0};
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    /* Enqueue buffer */
    if (-1 == xioctl(VIDIOC_QBUF, &buf, true)) {
        perror("VIDIOC_QBUF");
    }
}

static void recv_frame(int i) {
    struct v4l2_buffer buf = {0};
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    /* Dequeue the buffer */
    if(-1 == xioctl(VIDIOC_DQBUF, &buf, true)) {
        perror("Retrieving Frame");
        return;
    }
    
    state.brightness[i] = compute_brightness(buf.bytesused) / CAMERA_ILL_MAX;
}

static double compute_brightness(const unsigned int size) {
    double brightness = 0.0;
    /*
     * If greyscale -> increment by 1. 
     * If YUYV -> increment by 2: we only want Y! 
     */
    const int inc = 1 + (state.pixelformat == V4L2_PIX_FMT_YUYV);
    
    for (int i = 0; i < size; i += inc) {
        brightness += state.buf.start[i];
    }
    brightness /= state.width * state.height;
    return brightness;
}

static void free_all(void) {
    if (state.buf.length) {
        munmap(state.buf.start, state.buf.length);
    }
    if (state.device_fd != -1) {
        close(state.device_fd);
    }
    /* reset state */
    memset(&state, 0, sizeof(struct state));
}
