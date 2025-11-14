# Visual Studio Workspace Files

Normally excludes from source control, but here for a Visual Studio CMake project workaround:

## VSWorkspaceSettings.json

Without the `ExcludedItems` listed in this file, Visual Studio tries top be "helpful"
and set the default startup item to some project file found in the directory tree. This is undesired.
