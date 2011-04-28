#include <string.h>
#include <CL/cl.h>
#include "ufo-buffer.h"

G_DEFINE_TYPE(UfoBuffer, ufo_buffer, G_TYPE_OBJECT);

#define UFO_BUFFER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_BUFFER, UfoBufferPrivate))

#define UFO_BUFFER_ERROR ufo_buffer_error_quark()

enum UfoBufferError {
    UFO_BUFFER_ERROR_WRONG_SIZE
};

enum {
    PROP_0,
    PROP_FINISHED
};

typedef enum {
    NO_DATA,
    CPU_DATA_VALID,
    GPU_DATA_VALID
} datastates;

struct _UfoBufferPrivate {
    gint32      width;
    gint32      height;
    gsize       size;   /**< size in bytes */

    gboolean    finished;
    datastates  state;
    float       *cpu_data;
    cl_mem      gpu_data;
    cl_command_queue command_queue;
};

static void ufo_buffer_set_dimensions(UfoBuffer *buffer, gint32 width, gint32 height)
{
    g_return_if_fail(UFO_IS_BUFFER(buffer));
    buffer->priv->width = width;
    buffer->priv->height = height;
    buffer->priv->size = width * height * sizeof(float);
}

GQuark ufo_buffer_error_quark(void)
{
    return g_quark_from_static_string("ufo-buffer-error-quark");
}

/* 
 * Public Interface
 */

/**
 * \brief Create a new buffer with given dimensions
 * \public \memberof UfoBuffer
 *
 * \param[in] width Width of the two-dimensional buffer
 * \param[in] height Height of the two-dimensional buffer
 *
 * \return Buffer with allocated memory
 *
 * \note Filters should never allocate buffers on their own using this method
 * but use the UfoResourceManager method ufo_resource_manager_request_buffer().
 */
UfoBuffer *ufo_buffer_new(gint32 width, gint32 height)
{
    UfoBuffer *buffer = UFO_BUFFER(g_object_new(UFO_TYPE_BUFFER, NULL));
    ufo_buffer_set_dimensions(buffer, width, height);
    return buffer;
}


/**
 * \brief Retrieve dimension of buffer
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer A UfoBuffer
 * \param[out] width Width of the buffer
 * \param[out] height Height of the buffer
 */
void ufo_buffer_get_dimensions(UfoBuffer *buffer, gint32 *width, gint32 *height)
{
    g_return_if_fail(UFO_IS_BUFFER(buffer));
    *width = buffer->priv->width;
    *height = buffer->priv->height;
}

/**
 * \brief Associate the buffer with a given OpenCL memory object
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer A UfoBuffer
 * \param[in] mem The OpenCL memory object, that the host memory is synchronized
 *      with
 *
 * \note This does not actually copy the data from host to device. Same as
 *      ufo_buffer_new(), users should stay away from calling this on their own.
 */
void ufo_buffer_create_gpu_buffer(UfoBuffer *buffer, cl_mem mem)
{
    buffer->priv->gpu_data = mem;
}

/**
 * \brief Fill buffer with data
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer A UfoBuffer to fill the data with
 * \param[in] data User supplied data
 * \param[in] n Number of data elements
 * \param[in] error Pointer to GError*
 */
void ufo_buffer_set_cpu_data(UfoBuffer *buffer, float *data, gsize n, GError **error)
{
    const gsize num_bytes = buffer->priv->width * buffer->priv->height * sizeof(float);
    if ((n * sizeof(float)) > num_bytes) {
        g_set_error(error,
                UFO_BUFFER_ERROR,
                UFO_BUFFER_ERROR_WRONG_SIZE,
                "Trying to set more data than buffer dimensions allow");
        return;
    }
    if (buffer->priv->cpu_data == NULL)
        buffer->priv->cpu_data = g_malloc0(num_bytes);

    memcpy(buffer->priv->cpu_data, data, num_bytes);
    buffer->priv->state = CPU_DATA_VALID;
}

/**
 * \brief Spread raw data without increasing the contrast
 * \public \memberof UfoBuffer
 *
 * The fundamental data type of a UfoBuffer is one single-precision floating
 * point per pixel. To increase performance it is possible to load arbitrary
 * integer data with ufo_buffer_set_cpu_data() and convert that data with this
 * method.
 *
 * \param[in] buffer UfoBuffer object
 * \param[in] source_depth The original integer data type. This could be
 * UFO_BUFFER_DEPTH_8 for 8-bit data or UFO_BUFFER_DEPTH_16 for 16-bit data.
 * \param[in] n Number of data elements to consider
 */
void ufo_buffer_reinterpret(UfoBuffer *buffer, gint source_depth, gsize n)
{
    float *dst = buffer->priv->cpu_data;
    /* To save a memory allocation and several copies, we process data from back
     * to front. This is possible if src bit depth is at most half as wide as
     * the 32-bit target buffer. The processor cache should not be a problem. */
    if (source_depth == UFO_BUFFER_DEPTH_8) {
        guint8 *src = (guint8 *) buffer->priv->cpu_data;
        for (int index = (n-1); index >= 0; index--)
            dst[index] = src[index] / 255.0;
    }
    else if (source_depth == UFO_BUFFER_DEPTH_16) {
        guint16 *src = (guint16 *) buffer->priv->cpu_data;
        for (int index = (n-1); index >= 0; index--)
            dst[index] = src[index] / 65535.0;
    }
}

/*
 * \brief Get raw pixel data in a flat array (row-column format)
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer UfoBuffer object
 * \return Pointer to a float array of valid data
 */
float* ufo_buffer_get_cpu_data(UfoBuffer *buffer)
{
    UfoBufferPrivate *priv = UFO_BUFFER_GET_PRIVATE(buffer);

    switch (priv->state) {
        case CPU_DATA_VALID:
            break;
        case GPU_DATA_VALID:
            memset(priv->cpu_data, 0, priv->size);
            clEnqueueReadBuffer(priv->command_queue,
                                priv->gpu_data,
                                CL_TRUE,
                                0, priv->size,
                                priv->cpu_data,
                                0, NULL, NULL);
            priv->state = CPU_DATA_VALID;
            break;
        case NO_DATA:
            priv->cpu_data = g_malloc0(priv->size);
            priv->state = CPU_DATA_VALID;
            break;
    }

    return priv->cpu_data;
}

/**
 * \brief Set OpenCL memory object that is used to up and download data.
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer UfoBuffer object
 * \param[in] mem OpenCL memory object to be associated with this UfoBuffer
 */
void ufo_buffer_set_cl_mem(UfoBuffer *buffer, cl_mem mem)
{
    buffer->priv->gpu_data = mem;
}

/**
 * \brief Get OpenCL memory object that is used to up and download data.
 * \public \memberof UfoBuffer
 *
 * \param[in] buffer UfoBuffer object
 * \return OpenCL memory object associated with this UfoBuffer
 */
cl_mem ufo_buffer_get_gpu_data(UfoBuffer *buffer)
{
    UfoBufferPrivate *priv = UFO_BUFFER_GET_PRIVATE(buffer);

    switch (priv->state) {
        case CPU_DATA_VALID:
            clEnqueueWriteBuffer(priv->command_queue,
                                 priv->gpu_data,
                                 CL_TRUE,
                                 0, priv->size,
                                 priv->cpu_data,
                                 0, NULL, NULL);
            priv->state = GPU_DATA_VALID;
            break;
        case GPU_DATA_VALID:
            break;
        case NO_DATA:
            return NULL;
    }
    return priv->gpu_data;
}

void ufo_buffer_set_command_queue(UfoBuffer *self, cl_command_queue queue)
{
    self->priv->command_queue = queue;
}

cl_command_queue ufo_buffer_get_command_queue(UfoBuffer *self)
{
    return self->priv->command_queue;
}

/* 
 * Virtual Methods 
 */
static void ufo_filter_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoBuffer *buffer = UFO_BUFFER(object);

    switch (property_id) {
        case PROP_FINISHED:
            buffer->priv->finished = g_value_get_boolean(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoBuffer *buffer = UFO_BUFFER(object);

    switch (property_id) {
        case PROP_FINISHED:
            g_value_set_boolean(value, buffer->priv->finished);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_buffer_finalize(GObject *gobject)
{
    UfoBuffer *buffer = UFO_BUFFER(gobject);
    UfoBufferPrivate *priv = UFO_BUFFER_GET_PRIVATE(buffer);
    if (priv->cpu_data)
        g_free(priv->cpu_data);

    G_OBJECT_CLASS(ufo_buffer_parent_class)->finalize(gobject);
}

/*
 * Type/Class Initialization
 */
static void ufo_buffer_class_init(UfoBufferClass *klass)
{
    /* override methods */
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->set_property = ufo_filter_set_property;
    gobject_class->get_property = ufo_filter_get_property;
    gobject_class->finalize = ufo_buffer_finalize;

    /* install properties */
    GParamSpec *pspec;
    pspec = g_param_spec_boolean("finished",
            "Is last buffer with invalid data and no one follows",
            "Get finished prop",
            FALSE,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class,
            PROP_FINISHED,
            pspec);

    /* install private data */
    g_type_class_add_private(klass, sizeof(UfoBufferPrivate));
}

static void ufo_buffer_init(UfoBuffer *buffer)
{
    UfoBufferPrivate *priv;
    buffer->priv = priv = UFO_BUFFER_GET_PRIVATE(buffer);
    priv->width = -1;
    priv->height = -1;
    priv->cpu_data = NULL;
    priv->state = NO_DATA;
    priv->finished = FALSE;
}