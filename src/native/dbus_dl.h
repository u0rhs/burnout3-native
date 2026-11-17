// dbus_dl.h -- the few libdbus-1 calls the MPRIS reader (now_playing_mpris.cpp) and its test player
// (tests/test_now_playing.cpp) use, loaded at run time with dlopen: the game links no D-Bus library and starts the same
// on a system without one. The declarations follow libdbus-1's public headers (dbus/dbus-errors.h, dbus-message.h,
// dbus-connection.h, dbus-bus.h, dbus-protocol.h; its ABI has been stable since 1.0). Linux only.
#ifndef B3NATIVE_DBUS_DL_H
#define B3NATIVE_DBUS_DL_H

#if defined(__linux__)

#include <dlfcn.h>

#include <cstdint>
#include <string>
#include <type_traits>

namespace b3dbus {

using dbus_bool_t = std::uint32_t;
struct DBusConnection;
struct DBusMessage;
// DBusError: two strings, five one-bit fields in an unsigned int and a pointer (32 bytes on 64-bit); padded here
struct DBusError {
    const char* name;
    const char* message;
    unsigned int dummies;
    void* padding1;
    void* spare[4];
};
// DBusMessageIter: an opaque block the library fills (72 bytes on 64-bit with dbus 1.x); a larger one is harmless
struct DBusMessageIter {
    alignas(8) unsigned char bytes[256];
};
enum : int {
    kTypeInvalid = 0, kTypeString = 's', kTypeObjectPath = 'o', kTypeArray = 'a', kTypeVariant = 'v', kTypeDictEntry = 'e',
    kTypeUint32 = 'u', kTypeInt32 = 'i', kTypeBoolean = 'b',
};
enum : int { kBusSession = 0 };
enum : unsigned { kNameFlagDoNotQueue = 4 };
enum : int { kRequestNamePrimaryOwner = 1 };

struct Lib {
    void* handle = nullptr;
    void (*error_init)(DBusError*) = nullptr;
    void (*error_free)(DBusError*) = nullptr;
    dbus_bool_t (*error_is_set)(const DBusError*) = nullptr;
    dbus_bool_t (*threads_init_default)() = nullptr;
    DBusConnection* (*bus_get_private)(int, DBusError*) = nullptr;
    void (*connection_set_exit_on_disconnect)(DBusConnection*, dbus_bool_t) = nullptr;
    void (*connection_close)(DBusConnection*) = nullptr;
    void (*connection_unref)(DBusConnection*) = nullptr;
    DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
    dbus_bool_t (*message_append_args)(DBusMessage*, int, ...) = nullptr;
    DBusMessage* (*connection_send_with_reply_and_block)(DBusConnection*, DBusMessage*, int, DBusError*) = nullptr;
    void (*message_unref)(DBusMessage*) = nullptr;
    dbus_bool_t (*message_iter_init)(DBusMessage*, DBusMessageIter*) = nullptr;
    int (*message_iter_get_arg_type)(DBusMessageIter*) = nullptr;
    void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*message_iter_next)(DBusMessageIter*) = nullptr;
    void (*message_iter_get_basic)(DBusMessageIter*, void*) = nullptr;
    // the test player's (a service on the bus)
    int (*bus_request_name)(DBusConnection*, const char*, unsigned, DBusError*) = nullptr;
    dbus_bool_t (*connection_read_write)(DBusConnection*, int) = nullptr;
    DBusMessage* (*connection_pop_message)(DBusConnection*) = nullptr;
    dbus_bool_t (*message_is_method_call)(DBusMessage*, const char*, const char*) = nullptr;
    dbus_bool_t (*message_get_args)(DBusMessage*, DBusError*, int, ...) = nullptr;
    DBusMessage* (*message_new_method_return)(DBusMessage*) = nullptr;
    DBusMessage* (*message_new_error)(DBusMessage*, const char*, const char*) = nullptr;
    void (*message_iter_init_append)(DBusMessage*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*message_iter_open_container)(DBusMessageIter*, int, const char*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*message_iter_append_basic)(DBusMessageIter*, int, const void*) = nullptr;
    dbus_bool_t (*message_iter_close_container)(DBusMessageIter*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*connection_send)(DBusConnection*, DBusMessage*, std::uint32_t*) = nullptr;
    void (*connection_flush)(DBusConnection*) = nullptr;

    // Loads libdbus-1.so.3 (once per Lib); false and *why when it is not there or lacks a call.
    bool load(std::string* why) {
        if (handle) return true;
        void* h = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!h) h = dlopen("libdbus-1.so", RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            if (why) *why = "no D-Bus library (libdbus-1.so.3)";
            return false;
        }
        bool ok = true;
        auto sym = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, name));
            if (!fn) ok = false;
        };
        sym(error_init, "dbus_error_init");
        sym(error_free, "dbus_error_free");
        sym(error_is_set, "dbus_error_is_set");
        sym(threads_init_default, "dbus_threads_init_default");
        sym(bus_get_private, "dbus_bus_get_private");
        sym(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
        sym(connection_close, "dbus_connection_close");
        sym(connection_unref, "dbus_connection_unref");
        sym(message_new_method_call, "dbus_message_new_method_call");
        sym(message_append_args, "dbus_message_append_args");
        sym(connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
        sym(message_unref, "dbus_message_unref");
        sym(message_iter_init, "dbus_message_iter_init");
        sym(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
        sym(message_iter_recurse, "dbus_message_iter_recurse");
        sym(message_iter_next, "dbus_message_iter_next");
        sym(message_iter_get_basic, "dbus_message_iter_get_basic");
        sym(bus_request_name, "dbus_bus_request_name");
        sym(connection_read_write, "dbus_connection_read_write");
        sym(connection_pop_message, "dbus_connection_pop_message");
        sym(message_is_method_call, "dbus_message_is_method_call");
        sym(message_get_args, "dbus_message_get_args");
        sym(message_new_method_return, "dbus_message_new_method_return");
        sym(message_new_error, "dbus_message_new_error");
        sym(message_iter_init_append, "dbus_message_iter_init_append");
        sym(message_iter_open_container, "dbus_message_iter_open_container");
        sym(message_iter_append_basic, "dbus_message_iter_append_basic");
        sym(message_iter_close_container, "dbus_message_iter_close_container");
        sym(connection_send, "dbus_connection_send");
        sym(connection_flush, "dbus_connection_flush");
        if (!ok) {
            dlclose(h);
            if (why) *why = "the D-Bus library lacks a call this reader uses";
            return false;
        }
        handle = h;
        threads_init_default();   // other users in the process (SDL's) may share the library
        return true;
    }

    // A private session-bus connection that never ends the process when the bus goes away; null and *why
    DBusConnection* session(std::string* why) {
        DBusError e;
        error_init(&e);
        DBusConnection* c = bus_get_private(kBusSession, &e);
        if (!c) {
            if (why) *why = std::string("no session D-Bus") + (error_is_set(&e) && e.message ? std::string(" (") + e.message + ")" : std::string());
            error_free(&e);
            return nullptr;
        }
        error_free(&e);
        connection_set_exit_on_disconnect(c, 0);
        return c;
    }
    void end(DBusConnection* c) {
        if (!c) return;
        connection_close(c);
        connection_unref(c);
    }
};

}  // namespace b3dbus

#endif
#endif
