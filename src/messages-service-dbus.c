#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dbus/dbus-glib.h>
#include "messages-service-dbus.h"
#include "dbus-data.h"

typedef struct _MessageServiceDbusPrivate MessageServiceDbusPrivate;

struct _MessageServiceDbusPrivate
{
	gboolean dot;
	gboolean hidden;
};

#define MESSAGE_SERVICE_DBUS_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), MESSAGE_SERVICE_DBUS_TYPE, MessageServiceDbusPrivate))

static void message_service_dbus_class_init (MessageServiceDbusClass *klass);
static void message_service_dbus_init       (MessageServiceDbus *self);
static void message_service_dbus_dispose    (GObject *object);
static void message_service_dbus_finalize   (GObject *object);

static void _messages_service_server_watch  (void);
static gboolean _messages_service_server_attention_requested (MessageServiceDbus * self, gboolean * dot, GError ** error);
static gboolean _messages_service_server_icon_shown (MessageServiceDbus * self, gboolean * hidden, GError ** error);

#include "messages-service-server.h"

G_DEFINE_TYPE (MessageServiceDbus, message_service_dbus, G_TYPE_OBJECT);

static void
message_service_dbus_class_init (MessageServiceDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (MessageServiceDbusPrivate));

	object_class->dispose = message_service_dbus_dispose;
	object_class->finalize = message_service_dbus_finalize;

	dbus_g_object_type_install_info(MESSAGE_SERVICE_DBUS_TYPE, &dbus_glib__messages_service_server_object_info);

	return;
}

static void
message_service_dbus_init (MessageServiceDbus *self)
{
	DBusGConnection * connection = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
	dbus_g_connection_register_g_object(connection,
										INDICATOR_MESSAGES_DBUS_SERVICE_OBJECT,
										G_OBJECT(self));

	MessageServiceDbusPrivate * priv = MESSAGE_SERVICE_DBUS_GET_PRIVATE(self);

	priv->dot = FALSE;
	priv->hidden = FALSE;

	return;
}

static void
message_service_dbus_dispose (GObject *object)
{


	G_OBJECT_CLASS (message_service_dbus_parent_class)->dispose (object);
	return;
}

static void
message_service_dbus_finalize (GObject *object)
{


	G_OBJECT_CLASS (message_service_dbus_parent_class)->finalize (object);
	return;
}

MessageServiceDbus *
message_service_dbus_new (void)
{
	return MESSAGE_SERVICE_DBUS(g_object_new(MESSAGE_SERVICE_DBUS_TYPE, NULL));
}

/* DBus function to say that someone is watching */
static void
_messages_service_server_watch  (void)
{

}

/* DBus interface to request the private variable to know
   whether there is a green dot. */
static gboolean
_messages_service_server_attention_requested (MessageServiceDbus * self, gboolean * dot, GError ** error)
{
	MessageServiceDbusPrivate * priv = MESSAGE_SERVICE_DBUS_GET_PRIVATE(self);
	*dot = priv->dot;
	return TRUE;
}

/* DBus interface to request the private variable to know
   whether the icon is hidden. */
static gboolean
_messages_service_server_icon_shown (MessageServiceDbus * self, gboolean * hidden, GError ** error)
{
	MessageServiceDbusPrivate * priv = MESSAGE_SERVICE_DBUS_GET_PRIVATE(self);
	*hidden = priv->hidden;
	return TRUE;
}

void
message_server_dbus_set_attention (MessageServiceDbus * self, gboolean attention)
{
	MessageServiceDbusPrivate * priv = MESSAGE_SERVICE_DBUS_GET_PRIVATE(self);
	/* Do signal */
	priv->dot = attention;
	return;
}

void
message_server_dbus_set_icon (MessageServiceDbus * self, gboolean hidden)
{
	MessageServiceDbusPrivate * priv = MESSAGE_SERVICE_DBUS_GET_PRIVATE(self);
	/* Do signal */
	priv->hidden = hidden;
	return;
}
