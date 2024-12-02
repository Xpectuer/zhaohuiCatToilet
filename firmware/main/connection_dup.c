#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_vfs.h"
#include "errno.h"
#include "stdarg.h"
#include "sys/lock.h"

#define NUM_FDS CONFIG_LWIP_MAX_SOCKETS
static const char * TAG = "conn_dup";
static esp_vfs_id_t dup_vfs_id;
static bool dup_vfs_initialized = false;

//array of duplicate fds, -1 as unused
static int fds[NUM_FDS] = {[0 ... NUM_FDS-1] = -1};
static _lock_t fds_lock;

bool connection_dup_initialized(){
    return dup_vfs_initialized;
}

// works like dup(), duplicates an existing fd, but only for lwip tcp
// connections, must be closed before the original fd
int connection_dup(int conn_fd){
    if (conn_fd < 0){
        errno = EBADF;
        return -1;
    }
    if (!dup_vfs_initialized){
        errno = EINVAL;
        return -1;
    }
    // find free fd
    int fd = -1;
    _lock_acquire(&fds_lock);
    for (int i = 0; i < NUM_FDS; ++i)
        if (fds[i] == -1){
            fds[i] = conn_fd;
            fd = i;
            break;
        }
    _lock_release(&fds_lock);
    // no free fd
    if (-1 == fd){
        errno = EMFILE;
        return -1;
    }
    // allocate fd from vfs
    int global_fd = -1;
    esp_err_t err = esp_vfs_register_fd_with_local_fd(dup_vfs_id, fd, false, &global_fd);
    if (ESP_OK == err) {
        return global_fd;
    } else if (ESP_ERR_NO_MEM == err) {
        errno = EMFILE;
        goto fail;
    } else if (ESP_ERR_INVALID_ARG == err) {
        errno = EINVAL;
        goto fail;
    }

fail:
    // fail and release local fd
    fds[fd] = -1;
    return -1;
}

static ssize_t dup_write(int fd, const void * data, size_t size){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    return lwip_write(fds[fd], data, size);
}

static int dup_fstat(int fd, struct stat * st){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    /* set the stat mode to socket type */
    st->st_mode = S_IFSOCK;
    return 0;
}

static int dup_close(int fd){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    fds[fd] = -1;
    return 0;
}

static ssize_t dup_read(int fd, void * dst, size_t size){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    return lwip_read(fds[fd], dst, size);
}

static int dup_fcntl(int fd, int cmd, int arg){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    return lwip_fcntl(fds[fd], cmd, arg);
}

static int dup_ioctl(int fd, int cmd, va_list args){
    if (fd < 0 || fd >= NUM_FDS || fds[fd] < 0){
        errno = EBADF;
        return -1;
    }
    return lwip_ioctl(fds[fd], cmd, va_arg(args, void*));
}

#ifdef CONFIG_VFS_SUPPORT_SELECT
//static int dup_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout){
//}

//static void* dup_get_socket_select_semaphore(void){
    /* Calling this from the same process as select() will ensure that the semaphore won't be allocated from
     * ISR (lwip_stop_socket_select_isr).
     */
//    return (void *) sys_thread_sem_get();
//}

//static void dup_stop_socket_select(void *sem){
//}

//static void dup_stop_socket_select_isr(void *sem, BaseType_t *woken){
//}

#endif // CONFIG_VFS_SUPPORT_SELECT

// register vfs driver for duplicate fds
esp_err_t init_connection_dup(void){
    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = &dup_write,
        .open = NULL,
        .fstat = &dup_fstat,
        .close = &dup_close,
        .read = &dup_read,
        .fcntl = &dup_fcntl,
        .ioctl = &dup_ioctl,
#ifdef CONFIG_VFS_SUPPORT_SELECT
        //.socket_select = &dup_select,
        //.get_socket_select_semaphore = &dup_get_socket_select_semaphore,
        //.stop_socket_select = &dup_stop_socket_select,
        //.stop_socket_select_isr = &dup_stop_socket_select_isr,
#endif // CONFIG_VFS_SUPPORT_SELECT
    };
    ESP_RETURN_ON_ERROR(esp_vfs_register_with_id(&vfs, NULL, &dup_vfs_id), TAG, "fail to register vfs for connection_dup");
    _lock_init(&fds_lock);
    dup_vfs_initialized = true;
    return ESP_OK;
}

