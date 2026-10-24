#!/usr/bin/env python3
"""A stand-in for a desktop panel: a minimal org.kde.StatusNotifierWatcher that says a host is
registered, so WackGet puts up its tray icon on the session bus. Used by linux_desktop.py.

Prints "REGISTERED <sender> <service>" when an item registers. Commands on stdin, one per line:
  dump            -> "PROPS {...}" (Id, Title, Status, IconName, ...) and "MENU [(id, label, enabled)...]"
  click <label>   -> sends a dbusmenu "clicked" event to the item whose label (without "_") matches
"""
import sys, threading
from gi.repository import Gio, GLib

XML = """<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<method name="RegisterStatusNotifierHost"><arg type="s" direction="in"/></method>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
<signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
</interface></node>"""

bus = Gio.bus_get_sync(Gio.BusType.SESSION)
items = []

def method(conn, sender, path, iface, name, params, inv):
    if name == "RegisterStatusNotifierItem":
        svc = params.unpack()[0]
        if svc.startswith("/"):
            svc = sender + svc
        items.append((sender, svc))
        print("REGISTERED", sender, svc, flush=True)
        conn.emit_signal(None, path, iface, "StatusNotifierItemRegistered", GLib.Variant("(s)", (svc,)))
    inv.return_value(None)

def getprop(conn, sender, path, iface, name):
    if name == "RegisteredStatusNotifierItems":
        return GLib.Variant("as", [s for _, s in items])
    if name == "IsStatusNotifierHostRegistered":
        return GLib.Variant("b", True)
    return GLib.Variant("i", 0)

node = Gio.DBusNodeInfo.new_for_xml(XML)
bus.register_object("/StatusNotifierWatcher", node.interfaces[0], method, getprop, None)
Gio.bus_own_name_on_connection(bus, "org.kde.StatusNotifierWatcher", Gio.BusNameOwnerFlags.NONE, None, None)

def call(dest, path, iface, meth, args, rtype=None):
    return bus.call_sync(dest, path, iface, meth, args, rtype, Gio.DBusCallFlags.NONE, 3000, None)

def find_item_path(sender):
    # ayatana: path from the registration string, or introspect
    for s, svc in items:
        if s == sender and "/" in svc:
            return svc[svc.index("/"):]
    return None

def layout(sender, menu_path):
    r = call(sender, menu_path, "com.canonical.dbusmenu", "GetLayout", GLib.Variant("(iias)", (0, -1, [])))
    rev, (rid, props, children) = r.unpack()
    out = []
    for c in children:
        cid, cp, _ = c
        out.append((cid, cp.get("label", "-" if cp.get("type") == "separator" else ""), cp.get("enabled", True)))
    return out

def handle(line):
    parts = line.strip().split(" ", 1)
    if not items:
        print("NOITEM", flush=True); return
    sender, svc = items[-1]
    path = find_item_path(sender) or "/StatusNotifierItem"
    props = call(sender, path, "org.freedesktop.DBus.Properties", "GetAll",
                 GLib.Variant("(s)", ("org.kde.StatusNotifierItem",))).unpack()[0]
    menu = props.get("Menu")
    if parts[0] == "dump":
        print("PROPS", {k: props[k] for k in ("Id", "Title", "Status", "IconName", "IconThemePath", "Menu") if k in props}, flush=True)
        print("MENU", layout(sender, menu), flush=True)
    elif parts[0] == "click":
        for cid, label, en in layout(sender, menu):
            if label.replace("_", "") == parts[1]:
                call(sender, menu, "com.canonical.dbusmenu", "Event",
                     GLib.Variant("(isvu)", (cid, "clicked", GLib.Variant("i", 0), 0)))
                print("CLICKED", label, flush=True)
                return
        print("NOLABEL", parts[1], flush=True)

def reader():
    for line in sys.stdin:
        GLib.idle_add(handle, line)

threading.Thread(target=reader, daemon=True).start()
print("WATCHER UP", flush=True)
GLib.MainLoop().run()
