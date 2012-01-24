#include <gmodule.h>
#include <string.h>
#include <stdio.h>

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-sino-generator.h"

struct _UfoFilterSinoGeneratorPrivate {
    guint num_projections;
};

GType ufo_filter_sino_generator_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterSinoGenerator, ufo_filter_sino_generator, UFO_TYPE_FILTER);

#define UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_SINO_GENERATOR, UfoFilterSinoGeneratorPrivate))

enum {
    PROP_0,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *sino_generator_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_sino_generator_initialize(UfoFilter *filter)
{
    /* Here you can code, that is called for each newly instantiated filter */
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_sino_generator_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterSinoGeneratorPrivate *priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    /* We pop the very first image, to determine the size w*h of a projection.
     * We then have to allocate h sinogram buffers with a height of
     * num_projections and width w */
    gint32 width, height, sino_width;
    guint received = 1;
    UfoBuffer *input = ufo_channel_pop(input_channel);
    int num_dims = 0;
    int *dimensions = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dimensions);
    width = dimensions[0];
    height = dimensions[1];

    sino_width = width;
    const gint sino_height = priv->num_projections;
    const gint num_sinos = height;
    const gsize bytes_per_line = sino_width * sizeof(float);
    dimensions[0] = sino_width;
    dimensions[1] = sino_height;

    UfoBuffer **sinograms = g_malloc0(sizeof(UfoBuffer*) * num_sinos);
    for (gint i = 0; i < num_sinos; i++)
        sinograms[i] = ufo_resource_manager_request_buffer(manager, 2, dimensions, NULL, FALSE);

    /* First step: collect all projections and build sinograms */
    while ((received < priv->num_projections) || (input != NULL)) {
        float *src = ufo_buffer_get_cpu_data(input, command_queue);
        for (gint i = 0; i < num_sinos; i++) {
            float *dst = ufo_buffer_get_cpu_data(sinograms[i], command_queue);
            dst += (received-1)*sino_width;
            memcpy(dst, src + i*sino_width, bytes_per_line);
        }
        ufo_resource_manager_release_buffer(manager, input);
        input = ufo_channel_pop(input_channel);
        received++;
    }
    
    /* Second step: push them one by one */
    for (gint i = 0; i < num_sinos; i++)
        ufo_channel_push(output_channel, sinograms[i]);

    /* Third step: complete */
    ufo_channel_finish(output_channel);
    g_free(dimensions);
}

static void ufo_filter_sino_generator_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            self->priv->num_projections = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_sino_generator_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            g_value_set_int(value, self->priv->num_projections);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_sino_generator_class_init(UfoFilterSinoGeneratorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_sino_generator_set_property;
    gobject_class->get_property = ufo_filter_sino_generator_get_property;
    filter_class->initialize = ufo_filter_sino_generator_initialize;
    filter_class->process = ufo_filter_sino_generator_process;

    /* install properties */
    sino_generator_properties[PROP_NUM_PROJECTIONS] = 
        g_param_spec_int("num-projections",
            "Number of projections",
            "Number of projections corresponding to the sinogram height",
            0,   /* minimum */
            8192,   /* maximum */
            1,   /* default */
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_NUM_PROJECTIONS, sino_generator_properties[PROP_NUM_PROJECTIONS]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterSinoGeneratorPrivate));
}

static void ufo_filter_sino_generator_init(UfoFilterSinoGenerator *self)
{
    UfoFilterSinoGeneratorPrivate *priv = self->priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(self);
    priv->num_projections = 1;
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_SINO_GENERATOR, NULL);
}
