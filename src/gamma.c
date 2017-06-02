/**
 * Thanks to http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/ 
 * and to improvements made here: http://www.zombieprototypes.com/?p=210.
 **/

#include "../inc/gamma.h"
#include "../inc/polkit.h"
#include "../inc/udev.h"
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static unsigned short clamp(double x, double max);
static unsigned short get_red(int temp);
static unsigned short get_green(int temp);
static unsigned short get_blue(int temp);
static int get_temp(const unsigned short R, const unsigned short B);
static void drm_set_temperature(const int temp, const char *card, int *err);
static int drm_get_temperature(const char *card, int *err);

int method_setgamma(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    int temp, error = 0;
    const char *card = NULL;
    struct udev_device *dev = NULL;
    
    if (!check_authorization(m)) {
        sd_bus_error_set_errno(ret_error, EPERM);
        return -EPERM;
    }
    
    /* Read the parameters */
    int r = sd_bus_message_read(m, "si", &card, &temp);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    if (temp < 1000 || temp > 10000) {
        error = EINVAL;
    } else {
        get_udev_device(card, "drm", &ret_error, &dev);
        if (sd_bus_error_is_set(ret_error)) {
            return -sd_bus_error_get_errno(ret_error);
        }
        
        drm_set_temperature(temp, udev_device_get_devnode(dev), &error);
        udev_device_unref(dev);
    }
    
    if (error) {
        sd_bus_error_set_errno(ret_error, error);
        return -error;
    }
    
    printf("Gamma value set: %d\n", temp);
    return sd_bus_reply_method_return(m, "i", temp);
}

int method_getgamma(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    int error = 0;
    const char *card = NULL;
    struct udev_device *dev = NULL;
    
    /* Read the parameters */
    int r = sd_bus_message_read(m, "s", &card);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    get_udev_device(card, "drm", &ret_error, &dev);
    if (sd_bus_error_is_set(ret_error)) {
        return -sd_bus_error_get_errno(ret_error);
    }
    
    int temp = drm_get_temperature(udev_device_get_devnode(dev), &error);
    udev_device_unref(dev);
    
    if (error) {
        sd_bus_error_set_errno(ret_error, error);
        return -error;
    }
    
    printf("Current gamma value: %d\n", temp);
    
    return sd_bus_reply_method_return(m, "i", temp);
}

static unsigned short clamp(double x, double max) {
    if (x > max) { 
        return max; 
    }
    return x;
}

static unsigned short get_red(int temp) {
    if (temp <= 6500) {
        return 255;
    }
    const double a = 351.97690566805693;
    const double b = 0.114206453784165;
    const double c = -40.25366309332127;
    const double new_temp = ((double)temp / 100) - 55;
    
    return clamp(a + b * new_temp + c * log(new_temp), 255);
}

static unsigned short get_green(int temp) {
    double a, b, c;
    double new_temp;
    if (temp <= 6500) {
        a = -155.25485562709179;
        b = -0.44596950469579133;
        c = 104.49216199393888;
        new_temp = ((double)temp / 100) - 2;
    } else {
        a = 325.4494125711974;
        b = 0.07943456536662342;
        c = -28.0852963507957;
        new_temp = ((double)temp / 100) - 50;        
    }
    return clamp(a + b * new_temp + c * log(new_temp), 255);
}

static unsigned short get_blue(int temp) {
    if (temp <= 1900) {
        return 0;
    }
    
    if (temp < 6500) {
        const double new_temp = ((double)temp / 100) - 10;
        const double a = -254.76935184120902;
        const double b = 0.8274096064007395;
        const double c = 115.67994401066147;
        
        return clamp(a + b * new_temp + c * log(new_temp), 255);
    }
    return 255;
}

/* Thanks to: https://github.com/neilbartlett/color-temperature/blob/master/index.js */
static int get_temp(const unsigned short R, const unsigned short B) {
    int temperature;
    int min_temp = B == 255 ? 6500 : 1000; // lower bound
    int max_temp = R == 255 ? 6500 : 10000; // upper bound
    unsigned short testR, testB;
    
    /* Compute first temperature with same R and B value as parameters */
    do {
        temperature = (max_temp + min_temp) / 2;
        testR = get_red(temperature);
        testB = get_blue(temperature);
        if ((double) testB / testR > (double) B / R) {
            max_temp = temperature;
        } else {
            min_temp = temperature;
        }
    } while (testR != R || testB != B);
    
    /* try to fit value in 50-steps temp -> ie: instead of 5238, try 5200 or 5250 */
    if (temperature % 50 != 0) {
        int tmp_temp = temperature - temperature % 50;
        if (get_red(tmp_temp) == R && get_blue(tmp_temp) == B) {
            temperature = tmp_temp;
        } else {
            tmp_temp = temperature + 50 - temperature % 50;
            if (get_red(tmp_temp) == R && get_blue(tmp_temp) == B) {
                temperature = tmp_temp;
            }
        }
    }
    
    return temperature;
}

void drm_set_temperature(const int temp, const char *card, int *err) {
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        *err = errno;
        return;
    }
        
    if (drmSetMaster(fd)) {
        perror("SetMaster");
        *err = errno;
        goto end;
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        int crtc_count = res->count_crtcs;
    
        const double red = get_red(temp) / (double)255;
        const double green = get_green(temp) / (double)255;
        const double blue = get_blue(temp) / (double)255;
    
        for (int i = 0; i < crtc_count; i++) {
            int id = res->crtcs[i];
            drmModeCrtc *crtc_info = drmModeGetCrtc(fd, id);
            int ramp_size = crtc_info->gamma_size;
    
            uint16_t *r_gamma = calloc(ramp_size, sizeof(uint16_t));
            uint16_t *g_gamma = calloc(ramp_size, sizeof(uint16_t));
            uint16_t *b_gamma = calloc(ramp_size, sizeof(uint16_t));
            for (int j = 0; j < ramp_size; j++) {
                const double g = 65535.0 * j / ramp_size;
                r_gamma[j] = g * red;
                g_gamma[j] = g * green;
                b_gamma[j] = g * blue;
            }
    
            int r = drmModeCrtcSetGamma(fd, id, ramp_size, r_gamma, g_gamma, b_gamma);
            if (r) {
                *err = errno;
                perror("drmModeCrtcSetGamma");
            }
            free(r_gamma);
            free(g_gamma);
            free(b_gamma);
            drmModeFreeCrtc(crtc_info);
        }
        drmModeFreeResources(res);
    } else {
        *err = errno;
    }

    if (drmDropMaster(fd)) {
        perror("DropMaster");
        *err = errno;
    }

end:
    close(fd);
}

static int drm_get_temperature(const char *card, int *err) {
    int temp = -1;
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        *err = errno;
        return temp;
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        int id = res->crtcs[0];
        drmModeCrtc *crtc_info = drmModeGetCrtc(fd, id);
        int ramp_size = crtc_info->gamma_size;
        
        uint16_t *red = calloc(ramp_size, sizeof(uint16_t));
        uint16_t *green = calloc(ramp_size, sizeof(uint16_t));
        uint16_t *blue = calloc(ramp_size, sizeof(uint16_t));
        
        int r = drmModeCrtcGetGamma(fd, id, ramp_size, red, green, blue);
        if (r) {
            *err = errno;
            perror("drmModeCrtcSetGamma");
        } else {
            temp = get_temp(clamp(red[1], 255), clamp(blue[1], 255));
        }
        
        free(red);
        free(green);
        free(blue);
        
        drmModeFreeCrtc(crtc_info);
        drmModeFreeResources(res);
    } else {
        *err = errno;
    }
    close(fd);
    return temp;
}
