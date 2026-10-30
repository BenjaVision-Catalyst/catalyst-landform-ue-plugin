# Catalyst Landform for Unreal Engine

Catalyst Landform presents server-defined planetary terrain through Voxel Plugin.
The initial source provides the runtime module and explicit Foundation/Connect
dependencies. Spherical fields, curved stamp evaluation and the Designer will be
added through separately qualified changes.

The development host is `Landform/Projects/LandformLab` in catalyst-workspace.
Its staging script copies this repository into the project's ignored Plugins
directory alongside the current Connect plugin and Foundation runtime. Edit this
repository, close the lab editor, stage the plugins, then rebuild the host.

Runtime code uses the CL prefix. Editor and automation modules will be introduced
when they have responsibilities. Module registration owns no connections or world
state; those owners must have explicit GameInstance/world/planet context.

Commercial VPP sources and binaries are supplied through the vendor installer and
are not distributed by this repository.
