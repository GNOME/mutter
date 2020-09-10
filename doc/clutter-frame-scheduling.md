# Clutter Frame scheduling

`ClutterFrameClock` state diagram.

```mermaid
stateDiagram
    INIT --> SCHEDULED* : first frame scheduled
    IDLE --> SCHEDULED* : next frame scheduled
    SCHEDULED* --> IDLE : frame clock inhibited or mode changed
    SCHEDULED* --> SCHEDULED* : schedule upgraded to SCHEDULED_NOW
    SCHEDULED* --> DISPATCHED_ONE : begin an update
    DISPATCHED_ONE --> IDLE : frame was either presented or aborted with nothing to draw
    DISPATCHED_ONE --> DISPATCHED_ONE_AND_SCHEDULED* : entering triple buffering
    DISPATCHED_ONE_AND_SCHEDULED* --> SCHEDULED* : frame was either presented or aborted with nothing to draw
    DISPATCHED_ONE_AND_SCHEDULED* --> DISPATCHED_ONE : frame clock inhibited or mode changed
    DISPATCHED_ONE_AND_SCHEDULED* --> DISPATCHED_TWO : start a second concurrent frame
    DISPATCHED_TWO --> DISPATCHED_ONE : leaving triple buffering
```
