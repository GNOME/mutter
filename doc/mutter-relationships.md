# Compositor stage and hardware relationships

## Brief description of components

 - `MetaLogicalMonitor` is one monitor or more monitor occupying the same region of the compositor space. E.g. when mirroring two monitors, both belong to the same logical monitor.
 - `MetaMonitor` is a single physical monitor, but it can sometimes consist of more than one separate panel (for instance, 5K tiled monitors which literally require 2 cables due to lack of bandwidth)
 - `MetaOuptut` is a connector e.g. a DisplayPort connector or HDMI connector.
 - `MetaCrtc` represents a component on the display hardware that channels video memory to connectors.

## Entity relationship diagram

```mermaid
erDiagram
  MetaBackend ||--|| MetaMonitorManager : owns
  MetaBackend ||--|{ MetaGpu : owns
  MetaBackend ||--|| ClutterStage : owns
  MetaGpu ||--|{ MetaCrtc : owns
  MetaGpu ||--|{ MetaOutput : owns
  MetaCrtc |o..o{ MetaOutput : assigned
  MetaBackend ||--|{ MetaVirtualMonitor : owns
  MetaVirtualMonitor ||--|| MetaCrtc : owns
  MetaVirtualMonitor ||--|| MetaOutput : owns
  MetaMonitorManager ||--|{ MetaMonitor : owns
  MetaMonitorManager ||--|{ MetaLogicalMonitor : owns
  MetaLogicalMonitor ||..|{ MetaMonitor : has
  MetaMonitor ||..|{ MetaOutput : has
  ClutterStage ||--|{ ClutterStageView : has
  ClutterStageView ||..|| MetaCrtc : corresponds
  ClutterStageView ||--|| ClutterFrameClock : owns
  ClutterStageView ||..|| MetaLogicalMonitor : derive-scale
```

## Class diagrams

`MetaBackend`, `MetaGpu` and `MetaMonitorManager` class diagrams.

```mermaid
classDiagram
    MetaBackend <-- MetaBackendNative
    MetaBackend <-- MetaBackendX11
    class MetaBackend{
      MetaMonitorManager monitor_manager
      List~MetaGpu~ gpus
    }
    MetaGpu <-- MetaGpuKms
    MetaGpu <-- MetaGpuXrandr
    class MetaGpu{
      List~MetaOutput~
      List~MetaCrtc~
    }
    MetaMonitorManager <-- MetaMonitorManagerNative
    MetaMonitorManager <-- MetaMonitorManagerXrandr
    class MetaMonitorManager{
      List~MetaMonitor~ monitors
      List~MetaLogicalMonitor~ logical_monitors
    }
```

`MetaLogicalMonitor`, `MetaMonitor`, `MetaOutput` and `MetaCrtc` class diagrams.

```mermaid
classDiagram
    MetaLogicalMonitor
    class MetaLogicalMonitor{
      List~MetaMonitor~
    }
    MetaMonitor <-- MetaMonitorNormal
    MetaMonitor <-- MetaMonitorTiled
    class MetaMonitorNormal{
      MetaOutput output
    }
    class MetaMonitorTiled{
      List~MetaOutput~ output
    }
    MetaOutput <-- MetaOutputNative
    MetaOutputNative <-- MetaOutputKms
    MetaOutputNative <-- MetaOutputVirtual
    MetaOutput <-- MetaOutputXrandr
    MetaCrtc <-- MetaCrtcNative
    MetaCrtcNative <-- MetaCrtcKms
    MetaCrtcNative <-- MetaCrtcVirtual
    MetaCrtc <-- MetarCrtcXrandr
```

`ClutterStage` and `ClutterStageView` class diagram when using the Wayland session.

```mermaid
classDiagram
    class ClutterStage{
      List~ClutterStageView~
    }
    class ClutterStageView{
      MetaCrtc crtc
    }
```

`MetaKms` class diagram.

```mermaid
classDiagram
    class MetaKms{
      List~MetaKmsDevice~ devices
    }
    class MetaKmsDevice{
      List~MetaKmsConnector~
      List~MetaKmsCrtc~
      List~MetaKmsPlane~
      MetaKmsImplDevice impl_device
    }
    MetaKms "many" --> MetaKmsDevice : Owns
    MetaKmsDevice --> MetaKmsImplDevice : Owns
    MetaKmsImplDevice <-- MetaKmsImplDeviceAtomic
    MetaKmsImplDevice <-- MetaKmsImplDeviceSimple
```

## Native backend and mode setting

 * `MetaGpuKms`, `MetaCrtcKms` and `MetaOutputKms` are used for configuration.
 * `MetaKmsDevice`, `MetaKmsCrtc`, `MetaKmsConnector` and `MetaKmsPlane` are abstractions on top of kernel mode setting concepts.

```mermaid
erDiagram
  MetaBackendNative ||--|{ MetaGpuKms : owns
  MetaBackendNative ||--|| MetaKms : owns
  MetaKms ||--|{ MetaKmsDevice : owns
  MetaKmsDevice ||--|{ MetaKmsCrtc : owns
  MetaKmsDevice ||--|{ MetaKmsConnector : owns
  MetaKmsDevice ||--|{ MetaKmsPlane : owns
  MetaGpuKms ||--|{ MetaCrtcKms : owns
  MetaGpuKms ||--|{ MetaOutputKms : owns
  MetaCrtcKms |o..o{ MetaOutputKms : assigned
  MetaGpuKms |o..o| MetaKmsDevice : associated
  MetaCrtcKms |o..o| MetaKmsCrtc : associated
  MetaOutputKms |o..o| MetaKmsConnector : associated
```
