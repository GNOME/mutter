# KMS abstraction

The KMS abstraction consists of various building blocks for helping out with
interacting with the various drm API's, enabling users to use a
transactional API, aiming to hide all interaction with the underlying APIs.

The subsystem defines two separate contexts, the "main" context, and the
"impl" context. The main context is the context of which mutter as a whole
runs in. It uses the main GLib main loop and main context and always runs in
the main thread.

The impl context is where all underlying API is being executed. While in the
current state, it always runs in the main thread, the aim is to be able to
execute the impl context in a dedicated thread.

The public facing MetaKms API is always assumed to be executed from the main
context.

The KMS abstraction consists of the following public components:

### `MetaKms`

Main entry point; used by the native backend to create devices, post updates
etc.

### `MetaKmsDevice`

A device (usually /dev/dri/cardN, where N being a number). Used to get KMS
objects, such as connectors, CRTCs, planes, as well as basic meta data such
as device path etc.

### `MetaKmsCrtc`

Represents a CRTC. It manages a representation of the current CRTC state,
including current mode, coordinates, possible clones.

### `MetaKmsConnector`

Represents a connector, e.g. a display port connection. It also manages a
representation of the current state, including meta data such as physical
dimension of the connected, available modes, EDID, tile info etc. It also
contains helper functions for configuration, as well as methods for adding
configuration to a transaction (See `MetaKmsUpdate`).

### `MetaKmsPlane`

Represents a hardware plane. A plane is used to define the content of what
should be presented on a CRTC. Planes can either be primary planes, used as
a backdrop for CRTCs, overlay planes, and cursor planes.

### `MetaKmsMode`

Represents a mode a CRTC and connector can be configured with.
Represents both modes directly derived from the devices, as well as
fall back modes when the CRTC supports scaling.

### `MetaKmsUpdate`

A KMS transaction object, meant to be processed potentially atomically when
posted. An update consists of plane assignments, mode sets and KMS object
property entries. The user adds updates to the object, and then posts it via
MetaKms. It will then be processed by the MetaKms backend (See
`MetaKmsImpl`), potentially atomically. Each `MetaKmsUpdate` deals with
updating a single device.

There are also these private objects, without public facing API:

### `MetaKmsImpl`

The KMS impl context object, managing things in the impl context.

### `MetaKmsImplDevice`

An object linked to a `MetaKmsDevice`, but where it is executed in the impl
context. It takes care of the updating of the various KMS object (CRTC,
connector, ..) states.

This is an abstract type, with currently `MetaKmsImplDeviceSimple`
implementing mode setting and page flipping using legacy DRM API.

### `MetaKmsPageFlip`

A object representing a page flip. It's created when a page flip is queued,
and contains information necessary to provide feedback to the one requesting
the page flip.
