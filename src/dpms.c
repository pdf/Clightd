#ifdef DPMS_PRESENT

#include "../inc/dpms.h"
#include "../inc/polkit.h"
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define DPMS_DISABLED -1

struct dpms_timeout {
    CARD16 standby;
    CARD16 suspend;
    CARD16 off;
};

static int set_dpms_state(const char *display, const char *xauthority, int dpms_level);
static int get_dpms_timeouts(const char *display, const char *xauthority, struct dpms_timeout *t);
static int set_dpms_timeouts(const char *display, const char *xauthority, struct dpms_timeout *t);

static drmModeConnectorPtr get_active_connector(int fd, int connector_id) {
    drmModeConnectorPtr connector = drmModeGetConnector(fd, connector_id);
    
    if (connector) {
        if (connector->connection == DRM_MODE_CONNECTED 
            && connector->count_modes > 0 && connector->encoder_id != 0) {
            return connector;
        }
        drmModeFreeConnector(connector);
    }
    return NULL;
}

static drmModePropertyPtr drm_get_prop(int fd, drmModeConnectorPtr connector, const char *name) {
    drmModePropertyPtr props;
    
    for (int i = 0; i < connector->count_props; i++) {
        props = drmModeGetProperty(fd, connector->props[i]);
        if (!props) {
            continue;
        }
        if (!strcmp(props->name, name)) {
            return props;
        }
        drmModeFreeProperty(props);
    }
    return NULL;
}

/*
 * state will be one of:
 * DPMS Extension Power Levels
 * 0     DPMSModeOn          In use
 * 1     DPMSModeStandby     Blanked, low power
 * 2     DPMSModeSuspend     Blanked, lower power
 * 3     DPMSModeOff         Shut off, awaiting activity
 * 
 * Clightd returns -1 (DPMS_DISABLED) if dpms is disabled
 */
static void drm_get_dpms(int *state) {
    *state = DPMS_DISABLED;
    drmModeConnectorPtr connector;
    
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        for (int i = 0; i < res->count_connectors; i++) {
            connector = get_active_connector(fd, res->connectors[i]);
            if (!connector) {
                continue;
            }
        
            drmModePropertyPtr p = drm_get_prop(fd, connector, "DPMS");
            if (p) {
                /* prop_id is 2, it means it is second prop */
                *state = (int)connector->prop_values[p->prop_id - 1];
                printf("DPMS: %d\n", *state);
                drmModeFreeProperty(p);
                drmModeFreeConnector(connector);
                break;
            }
        }
        drmModeFreeResources(res);
    }
    close(fd);
}

static void drm_set_dpms(int level) {
    drmModeConnectorPtr connector;
    drmModePropertyPtr prop;
    
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        for (int i = 0; i < res->count_connectors; i++) {
            connector = get_active_connector(fd, res->connectors[i]);
            if (!connector) {
                continue;
            }
            
            prop = drm_get_prop(fd, connector, "DPMS");
            if (!prop) {
                drmModeFreeConnector(connector);
                continue;
            }
            if (drmModeConnectorSetProperty(fd, connector->connector_id, prop->prop_id, level)) {
                perror("drmModeConnectorSetProperty");
            }
            drmModeFreeProperty(prop);
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(res);
    }
    close(fd);
}

int method_getdpms(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *display = NULL, *xauthority = NULL;
        
    /* Read the parameters */
    int r = sd_bus_message_read(m, "ss", &display, &xauthority);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    int dpms_state;
    drm_get_dpms(&dpms_state);
    
    if (dpms_state == DPMS_DISABLED) {
        printf("Dpms is currently disabled.\n");
    } else {
        printf("Current dpms state: %d\n", dpms_state);
    }
    return sd_bus_reply_method_return(m, "i", dpms_state);
}

int method_setdpms(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *display = NULL, *xauthority = NULL;
    int level;
    
    /* Require polkit auth */
    if (!check_authorization(m)) {
        sd_bus_error_set_errno(ret_error, EPERM);
        return -EPERM;
    }
    
    /* Read the parameters */
    int r = sd_bus_message_read(m, "ssi", &display, &xauthority, &level);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    drm_set_dpms(level);
    printf("New dpms state: %d\n", level);
    return sd_bus_reply_method_return(m, "i", level);
    
    /* 0 -> DPMSModeOn, 3 -> DPMSModeOff */
    if (level < 0 || level > 3) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Wrong DPMS level value.");
        return -EINVAL;
    }
    
    int err = set_dpms_state(display, xauthority, level);
    if (err) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Could not open X screen.");
        return err;
    }
    
    printf("New dpms state: %d\n", level);
    return sd_bus_reply_method_return(m, "i", level);
}

int method_getdpms_timeouts(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *display = NULL, *xauthority = NULL;
    
    /* Read the parameters */
    int r = sd_bus_message_read(m, "ss", &display, &xauthority);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    struct dpms_timeout t = {0};
    int error = get_dpms_timeouts(display, xauthority, &t);
    if (error == DPMS_DISABLED) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Dpms is disabled.");
        return DPMS_DISABLED;
    }
    
    printf("Current dpms timeouts:\tStandby: %ds\tSuspend: %ds\tOff:%ds.\n", t.standby, t.suspend, t.off);
    return sd_bus_reply_method_return(m, "iii", t.standby, t.suspend, t.off);
}

int method_setdpms_timeouts(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *display = NULL, *xauthority = NULL;
    
    /* Require polkit auth */
    if (!check_authorization(m)) {
        sd_bus_error_set_errno(ret_error, EPERM);
        return -EPERM;
    }
    
    /* Read the parameters */
    int standby, suspend, off;
    int r = sd_bus_message_read(m, "ssiii", &display, &xauthority, &standby, &suspend, &off);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    if (standby < 0 || suspend < 0 || off < 0) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Wrong DPMS timeouts values.");
        return -EINVAL;
    }
    
    struct dpms_timeout t = { .standby = standby, .suspend = suspend, .off = off };
    int err = set_dpms_timeouts(display, xauthority, &t);
    if (err) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Could not open X screen.");
        return err;
    }
    
    printf("New dpms timeouts:\tStandby: %ds\tSuspend: %ds\tOff:%ds.\n", t.standby, t.suspend, t.off);
    return sd_bus_reply_method_return(m, "iii", standby, suspend, off);
}

int set_dpms_state(const char *display, const char *xauthority, int dpms_level) {
    int ret = 0;
    
    /* set xauthority cookie */
    setenv("XAUTHORITY", xauthority, 1);
    
    Display *dpy = XOpenDisplay(display);
    if (dpy && DPMSCapable(dpy)) {
        DPMSEnable(dpy);
        DPMSForceLevel(dpy, dpms_level);
        XFlush(dpy);
    } else {
        ret = -2;
    }
    
    /* Drop xauthority cookie */
    unsetenv("XAUTHORITY");
    
    if (dpy) {
        XCloseDisplay(dpy);
    }
    
    return ret;
}

int get_dpms_timeouts(const char *display, const char *xauthority, struct dpms_timeout *t) {
    BOOL onoff;
    CARD16 s;
    int ret = -2;
    
    /* set xauthority cookie */
    setenv("XAUTHORITY", xauthority, 1);
    
    Display *dpy = XOpenDisplay(display);
    if (dpy &&  DPMSCapable(dpy)) {
        DPMSInfo(dpy, &s, &onoff);
        if (onoff) {
            DPMSGetTimeouts(dpy, &(t->standby), &(t->suspend), &(t->off));
            ret = 0;
        } else {
            ret = DPMS_DISABLED;
        }
    }
    
    /* Drop xauthority cookie */
    unsetenv("XAUTHORITY");
    
    if (dpy) {
        XCloseDisplay(dpy);
    }
    
    return ret;
}

int set_dpms_timeouts(const char *display, const char *xauthority, struct dpms_timeout *t) {
    int ret = 0;
    
    /* set xauthority cookie */
    setenv("XAUTHORITY", xauthority, 1);
    
    Display *dpy = XOpenDisplay(display);
    if (dpy && DPMSCapable(dpy)) {
        DPMSEnable(dpy);
        DPMSSetTimeouts(dpy, t->standby, t->suspend, t->off);
        XFlush(dpy);
    } else {
        ret = -2;
    }
    
    /* Drop xauthority cookie */
    unsetenv("XAUTHORITY");
    
    if (dpy) {
        XCloseDisplay(dpy);
    }
    
    return ret;
}

#endif
