// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
// Keep this embedded manifest identical to description.json.
constexpr const char DESCRIPTION[] = R"json({
  "contractVersion": 1,
  "firmwareVersion": "0.3.0",
  "name": "Two custom props",
  "configuration": "firmware-owned",
  "diagnostics": {
    "transitions": false
  },
  "props": [
    {
      "id": "0210a18f-c31f-45b7-a35a-e20eb82a6c11",
      "name": "Three taps",
      "signals": [
        {
          "id": "completed",
          "name": "Completed"
        }
      ],
      "commands": [
        {
          "id": "complete",
          "name": "Complete prop",
          "testable": true
        },
        {
          "id": "reset",
          "name": "Reset",
          "testable": true
        },
        {
          "id": "pulse",
          "name": "Pulse LED",
          "testable": true
        }
      ],
      "state": [
        {
          "id": "solved",
          "name": "Solved",
          "type": "boolean"
        },
        {
          "id": "output",
          "name": "LED active",
          "type": "boolean"
        }
      ],
      "completion": {
        "signal": "completed",
        "command": "complete"
      }
    },
    {
      "id": "91ff0128-8086-473f-8187-fd36ff920b04",
      "name": "Hold button",
      "signals": [
        {
          "id": "completed",
          "name": "Completed"
        }
      ],
      "commands": [
        {
          "id": "complete",
          "name": "Complete prop",
          "testable": true
        },
        {
          "id": "reset",
          "name": "Reset",
          "testable": true
        },
        {
          "id": "pulse",
          "name": "Pulse LED",
          "testable": true
        }
      ],
      "state": [
        {
          "id": "solved",
          "name": "Solved",
          "type": "boolean"
        },
        {
          "id": "output",
          "name": "LED active",
          "type": "boolean"
        }
      ],
      "completion": {
        "signal": "completed",
        "command": "complete"
      }
    }
  ]
})json";
