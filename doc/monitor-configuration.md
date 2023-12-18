# Monitor configuration

## File locations

Monitor configurations are stored as XML files called `monitors.xml` on the file
system. There are two types of locations for the XML file: the system level and
the user level.

The directories for system level configuration is defined in accordance to the
$XDG_CONFIG_DIRS environment variable defined in the XDG Base Directory
Specification. The default is `/etc/xdg/monitors.xml`.

The directory for the user level configuration is defined in accordance to the
$XDG_CONFIG_HOME environment variable defined in the XDG Base Directory
Specification. The default is `~/.config/monitors.xml`

## File contents

A configuration file consists of an XML document with the root element
`<monitors version="2">`. In this document multiple configurations are stored as
individual `<configuration/>` elements containing all the details of the monitor
setup. The `version` attribute must be set to `"2"`.

Each configuration corresponds to a specific hardware setup, where a given set
of monitors are connected to the computer. There can only be one configuration
per hardware setup.

## Writing configuration

Monitor configurations are managed by Mutter via the Display panel in Settings,
which uses a D-Bus API to communicate with Mutter. Each time a new configuration
is applied and accepted, the user level configuration file is replaced with
updated content.

Previously defined monitor configurations for hardware state other than the
current are left intact.

## Configuration policy

The monitor configuration policy determines how Mutter configures monitors. This
can mean for example in what order configuration files should be preferred, or
whether configuration via Settings (i.e. D-Bus) should be allowed.

The default policy is to prioritize configurations defined in the user level
configuration file, and to allow configuring via D-Bus.

Changing the policy is possible by manually adding a `<policy/>` element inside
the `<monitors version="2"/>` element in the `monitors.xml` file. Note that
there may only be one `<policy/>` element in each configuration file.

### Changing configuration file priority policy

To change the order of configuration file priority, or to disable configuration
files completely, add a `<stores/>` element inside the `<policy/>` element
described above.

In this element, the file policy is defined by a `<stores/>` element, which
lists stores with the order according to prioritization. Each store is specified
using a `<store/>` element with either `system` or `user` as the content.

#### Example of only reading monitor configuration from the system level file:

```xml
<monitors version="2">
  <policy>
    <stores>
      <store>system</store>
    </stores>
  </policy>
</monitors>
```

#### Example of reversing the priority of monitor configuration:

```xml
<monitors version="2">
  <policy>
    <stores>
      <store>user</store>
      <store>system</store>
    </stores>
  </policy>
</monitors>
```

### Changing D-Bus configuration policy

D-Bus configureability can be configured using a `<dbus/>` element in the
`<policy/>` element. It's content should either be `yes` or `no` depending on
whether monitor configuration via D-Bus should be enabled or disable.

#### Example of how to disable monitor configuration via D-Bus:

```xml
<monitors version="2">
  <policy>
    <dbus>no</dbus>
  </policy>
</monitors>
```
