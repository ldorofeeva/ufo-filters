/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-non-local-means-task.h"


struct _UfoNonLocalMeansTaskPrivate {
    guint search_radius;
    guint patch_radius;
    gfloat sigma;
    cl_kernel kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoNonLocalMeansTask, ufo_non_local_means_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_NON_LOCAL_MEANS_TASK, UfoNonLocalMeansTaskPrivate))

enum {
    PROP_0,
    PROP_SEARCH_RADIUS,
    PROP_PATCH_RADIUS,
    PROP_SIGMA,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_non_local_means_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_NON_LOCAL_MEANS_TASK, NULL));
}

static void
ufo_non_local_means_task_setup (UfoTask *task,
                                UfoResources *resources,
                                GError **error)
{
    UfoNonLocalMeansTaskPrivate *priv;

    priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE (task);
    priv->kernel = ufo_resources_get_kernel (resources, "nlm.cl", "nlm_noise_reduction", NULL, error);

    if (priv->kernel)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernel), error);
}

static void
ufo_non_local_means_task_get_requisition (UfoTask *task,
                                          UfoBuffer **inputs,
                                          UfoRequisition *requisition,
                                          GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_non_local_means_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_non_local_means_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_non_local_means_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_non_local_means_task_process (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoBuffer *output,
                                  UfoRequisition *requisition)
{
    UfoNonLocalMeansTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;

    priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (guint), &priv->search_radius));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (guint), &priv->patch_radius));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (gfloat), &priv->sigma));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

    return TRUE;
}


static void
ufo_non_local_means_task_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
    UfoNonLocalMeansTaskPrivate *priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SEARCH_RADIUS:
            priv->search_radius = g_value_get_uint (value);
            break;
        case PROP_PATCH_RADIUS:
            {
                guint v = g_value_get_uint (value);

                if (!(v % 2)) {
                    g_printerr ("Patch radius must be odd, increasing by one\n");
                    v++;
                }

                priv->patch_radius = v;
            }
            break;
        case PROP_SIGMA:
            priv->sigma = g_value_get_float (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_non_local_means_task_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
    UfoNonLocalMeansTaskPrivate *priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SEARCH_RADIUS:
            g_value_set_uint (value, priv->search_radius);
            break;
        case PROP_PATCH_RADIUS:
            g_value_set_uint (value, priv->patch_radius);
            break;
        case PROP_SIGMA:
            g_value_set_float (value, priv->sigma);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_non_local_means_task_finalize (GObject *object)
{
    UfoNonLocalMeansTaskPrivate *priv;

    priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    G_OBJECT_CLASS (ufo_non_local_means_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_non_local_means_task_setup;
    iface->get_num_inputs = ufo_non_local_means_task_get_num_inputs;
    iface->get_num_dimensions = ufo_non_local_means_task_get_num_dimensions;
    iface->get_mode = ufo_non_local_means_task_get_mode;
    iface->get_requisition = ufo_non_local_means_task_get_requisition;
    iface->process = ufo_non_local_means_task_process;
}

static void
ufo_non_local_means_task_class_init (UfoNonLocalMeansTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_non_local_means_task_set_property;
    oclass->get_property = ufo_non_local_means_task_get_property;
    oclass->finalize = ufo_non_local_means_task_finalize;

    properties[PROP_SEARCH_RADIUS] =
        g_param_spec_uint ("search-radius",
            "Search radius in pixels",
            "Search radius in pixels",
            10, 8192, 10,
            G_PARAM_READWRITE);

    properties[PROP_PATCH_RADIUS] =
        g_param_spec_uint ("patch-radius",
            "Search radius in pixels",
            "Search radius in pixels",
            3, 27, 3,
            G_PARAM_READWRITE);

    properties[PROP_SIGMA] =
        g_param_spec_float ("sigma",
            "Sigma",
            "Sigma",
            0.0f, G_MAXFLOAT, 0.1f,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoNonLocalMeansTaskPrivate));
}

static void
ufo_non_local_means_task_init(UfoNonLocalMeansTask *self)
{
    self->priv = UFO_NON_LOCAL_MEANS_TASK_GET_PRIVATE(self);

    self->priv->search_radius = 10;
    self->priv->patch_radius = 3;
    self->priv->sigma = 0.1f;
}
