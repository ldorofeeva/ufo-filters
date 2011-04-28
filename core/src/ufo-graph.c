#include <glib.h>
#include <ethos/ethos.h>
#include <json-glib/json-glib.h>

#include "ufo-graph.h"
#include "ufo-sequence.h"
#include "ufo-split.h"
#include "ufo-resource-manager.h"
#include "ufo-element.h"

G_DEFINE_TYPE(UfoGraph, ufo_graph, G_TYPE_OBJECT);

#define UFO_GRAPH_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_GRAPH, UfoGraphPrivate))

struct _UfoGraphPrivate {
    EthosManager        *ethos;
    UfoResourceManager  *resource_manager;
    UfoElement          *root_container;
    GHashTable          *plugins;   /**< maps from gchar* to EthosPlugin* */
};


static UfoFilter *ufo_graph_get_filter(UfoGraph *self, const gchar *plugin_name)
{
    return UFO_FILTER(g_hash_table_lookup(self->priv->plugins, plugin_name));
}

static UfoElement *ufo_graph_build_split(JsonObject *object)
{
    UfoSplit *container = ufo_split_new();

    if (json_object_has_member(object, "mode")) {
        const char *mode = json_object_get_string_member(object, "mode");
        g_object_set(container, "mode", mode, NULL);
    }
    
    return UFO_ELEMENT(container);
}

static void ufo_graph_handle_json_prop(JsonObject *object, const gchar *name, JsonNode *node, gpointer user)
{
    GValue val = { 0, };
    json_node_get_value(node, &val);
    g_object_set_property(G_OBJECT(user), name, &val);
    g_value_unset(&val);
}

static void ufo_graph_build(UfoGraph *self, JsonNode *node, UfoElement **container)
{
    JsonObject *object = json_node_get_object(node);
    if (json_object_has_member(object, "type")) {
        const char *type = json_object_get_string_member(object, "type");

        if (g_strcmp0(type, "filter") == 0) {
            /* FIXME: we should check that there is a corresponding "plugin"
             * object available */
            const gchar *plugin_name = json_object_get_string_member(object, "plugin");
            UfoFilter *filter = ufo_graph_get_filter(self, plugin_name);
            if (filter != NULL) {
                /* TODO: ask the plugin how many/what kind of buffers we need,
                 * for now just reserve a queue for a single output */
                /*ufo_filter_set_resource_manager(filter, self->priv->resource_manager);*/
                ufo_filter_initialize(filter, self->priv->resource_manager);

                if (json_object_has_member(object, "properties")) {
                    JsonObject *prop_object = json_object_get_object_member(object, "properties");
                    json_object_foreach_member(prop_object, 
                                               ufo_graph_handle_json_prop,
                                               filter);
                }

                ufo_container_add_element(UFO_CONTAINER(*container), UFO_ELEMENT(filter));
            }
            else
                g_warning("Couldn't find plugin '%s'", plugin_name);
        }
        else {
            UfoElement *new_container = NULL;
            if (g_strcmp0(type, "sequence") == 0)
                new_container = UFO_ELEMENT(ufo_sequence_new());
            else if (g_strcmp0(type, "split") == 0)
                new_container = ufo_graph_build_split(object);

            /* Neither filter, sequence or split... just return */
            if (new_container == NULL)
                return;
            
            /* If we have a root container, assign the newly created one */
            if (*container == NULL)
                *container = new_container;
            else
                ufo_container_add_element(UFO_CONTAINER(*container), new_container);

            JsonArray *elements = json_object_get_array_member(object, "elements");
            for (guint i = 0; i < json_array_get_length(elements); i++) 
                ufo_graph_build(self, json_array_get_element(elements, i), &new_container);

            /* After adding all sub-childs, we need to get the updated output of
             * the new container and use it as our own new output */
            GAsyncQueue *prev = ufo_element_get_output_queue(new_container);
            ufo_element_set_output_queue(*container, prev);
        }
    }
}

/* 
 * Public Interface
 */
/**
 * \brief Read a JSON configuration file to build a static UfoGraph
 * \public \memberof UfoGraph
 * \param[in] filename Path and filename to the JSON file
 * \return A UfoGraph object build from the JSON description or NULL if JSON
 * file could not be parsed
 */
UfoGraph *ufo_graph_new_from_json(const gchar *filename)
{
    UfoGraph *self = ufo_graph_new();
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    json_parser_load_from_file(parser, filename, &error);
    if (error) {
        g_message("Parse error: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    ufo_graph_build(self, json_parser_get_root(parser), &self->priv->root_container);
    g_object_unref(parser);
    return self;
}

/**
 * \brief Start execution of all UfoElements in the UfoGraph until no more data
 * is produced
 * \public \memberof UfoGraph
 * \param[in] graph The UfoGraph to be executed
 */
void ufo_graph_run(UfoGraph *graph)
{
    ufo_element_print(UFO_ELEMENT(graph->priv->root_container));
    ufo_element_process(UFO_ELEMENT(graph->priv->root_container));
}

/**
 * \brief Create a new UfoGraph instance
 * \return A UfoGraph
 * \public \memberof UfoGraph
 */
UfoGraph *ufo_graph_new()
{
    return g_object_new(UFO_TYPE_GRAPH, NULL);
}

/* 
 * Virtual Methods 
 */
static void ufo_graph_dispose(GObject *object)
{
    UfoGraph *self = UFO_GRAPH(object);

    GObject *objects[] = {
        G_OBJECT(self->priv->resource_manager),
        G_OBJECT(self->priv->root_container),
        /*G_OBJECT(self->priv->ethos),*/
        NULL
    };

    int i = 0;
    while (objects[i] != NULL)
        g_object_unref(objects[i++]);


    if (self->priv->plugins) {
        g_hash_table_destroy(self->priv->plugins);
        self->priv->plugins = NULL;
    }

    G_OBJECT_CLASS(ufo_graph_parent_class)->dispose(object);
}

static void ufo_graph_add_plugin(gpointer data, gpointer user_data)
{
    EthosPluginInfo *info = (EthosPluginInfo *) data;
    UfoGraphPrivate *priv = (UfoGraphPrivate *) user_data;

    g_message("Load filter: %s", ethos_plugin_info_get_name(info));

    g_hash_table_insert(priv->plugins, 
        (gpointer) ethos_plugin_info_get_name(info),
        ethos_manager_get_plugin(priv->ethos, info));
}

/*
 * Type/Class Initialization
 */
static void ufo_graph_class_init(UfoGraphClass *klass)
{
    /* override methods */
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = ufo_graph_dispose;

    /* install private data */
    g_type_class_add_private(klass, sizeof(UfoGraphPrivate));
}

static void ufo_graph_init(UfoGraph *self)
{
    UfoGraphPrivate *priv;
    self->priv = priv = UFO_GRAPH_GET_PRIVATE(self);

    /* TODO: determine directories in a better way */
    gchar *plugin_dirs[] = { "../filters", NULL };

    priv->ethos = ethos_manager_new_full("UFO", plugin_dirs);
    ethos_manager_initialize(priv->ethos);

    priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
    GList *plugin_info = ethos_manager_get_plugin_info(priv->ethos);

    g_list_foreach(plugin_info, &ufo_graph_add_plugin, priv);
    g_list_free(plugin_info);

    priv->resource_manager = ufo_resource_manager_new();
    priv->root_container = NULL;
}

