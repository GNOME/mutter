# Clutter Frame scheduling

`ClutterFrameClock` state diagram.

```mermaid
stateDiagram
    Init --> Scheduled/ScheduledNow : schedule update() -> now
    Idle --> Scheduled/ScheduledNow : schedule update() -> given presentation time
    Scheduled/ScheduledNow --> Dispatching : target time hit
    Dispatching --> PendingPresented : queued page flip
    Dispatching --> Idle : no queued page flip
    PendingPresented --> Scheduled/ScheduledNow : page flipped, if recent schedule update
    PendingPresented --> Idle : page flipped
```
