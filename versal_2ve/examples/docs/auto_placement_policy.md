# Auto Placement Policy

## Overview

The ve2-xc2ve3558 NPU contains 36 AI Engine columns that can be shared among multiple models running simultaneously. When an application loads a model, the runtime must decide **which columns that model occupies** — a decision called _column placement_. Getting this right determines whether multiple models can run truly in parallel, share columns by taking turns, or fail to load altogether.

This document explains how column placement works, how the runtime decides where to assign a model, and what rules govern concurrent execution. Understanding this policy is essential for designing multi-model applications that use NPU resources efficiently.

---

## Step 1 — Understand What Interface You Are Using

The first thing that shapes column placement is the programming interface your application uses. VART and ONNX RT handle column assignment in fundamentally different ways, and this difference drives everything else in this document.

The table below summarises the key distinctions:

| Feature           | VART Interface                                                                          | ONNX RT Interface                             |
| ----------------- | --------------------------------------------------------------------------------------- | --------------------------------------------- |
| Model offload     | Fully offloaded (single partition)                                                      | Partial or full; may span multiple partitions |
| Column selection  | Optional user-controlled (explicit start column); defaults to auto placement if not set | Automatic (auto placement)                    |
| Partition support | Single partition only                                                                   | Single or multiple partitions                 |

In a **VART** application, a model is always fully offloaded as a single partition. The application _may_ explicitly specify which NPU start column to use — but this is optional. If no start column is provided, VART falls back to the same auto placement policy described in this document.

In an **ONNX RT** application, the VitisAI Execution Provider may offload the model fully or split it across multiple partitions. When a model spans multiple partitions, each partition requires its own contiguous column range on the NPU. Manually specifying a start column for every partition — while ensuring none of them overlap with each other or with other models already running — becomes increasingly complex and fragile as the number of models and partitions grows. Column placement is therefore always handled **automatically** by the VitisAI Execution Provider. This automatic mechanism is what the rest of this document describes.

---

## Step 2 — Know How Many Columns Your Model Needs

The number of AI Engine columns a model occupies is determined at **compile time** based on the `dp_size` and `tp_size` parameters provided during compilation:

```
aie_column_footprint = tp_size × dp_size × 4
```

- **`tp_size`** — Tensor parallelism size: how many column groups the model is split across horizontally.
- **`dp_size`** — Data parallelism size: how many independent replicas of the model run in parallel.
- **`4`** — Each partition occupies exactly 4 AI Engine columns.

For example, a model compiled with `tp_size=1, dp_size=1` occupies 4 columns; one with `tp_size=6, dp_size=1` occupies 24 columns. This column footprint is the unit the placement engine works with — every placement decision from this point forward is based on it.

---

## Step 3 — See How the Runtime Places the Model

With the column footprint known, the VitisAI Execution Provider applies the following policy each time a model session is created:

- **Spatial placement**: If a contiguous range of free columns matching the model's `aie_column_footprint` exists, the model is assigned to that range. It runs **concurrently** with all other spatially placed models — true parallel execution.

- **Temporal sharing**: If no free contiguous range is available, the runtime looks for an existing model that occupies the **same number of columns**. If any such model is found, the new model time-shares those columns with it — the two models take turns using the same hardware.

> **Important**: Temporal sharing is only possible when a model with the same number of columns already exists. If no match is found and no free range is available, placement **fails with an error**. There is no fallback or partial placement.

The two mechanisms work together: spatial placement fills up the NPU greedily, and temporal sharing allows additional models to stack on top of already-placed models, as long as the geometry matches.

---

## Step 4 — Trace Through Real Examples

The following examples show how this policy plays out when multiple models are loaded in sequence.

### Example 1: Mixed-Partition Models

Consider four models loaded one after another into a 36-column NPU. Each partition occupies 4 columns, so total columns = partitions × 4:

| Model   | Partitions | Columns (partitions × 4) | Placement                   |
| ------- | ---------- | ------------------------ | --------------------------- |
| Model 1 | 1          | 4                        | Spatial: 0–3                |
| Model 2 | 4          | 16                       | Spatial: 4–19               |
| Model 3 | 4          | 16                       | Spatial: 20–35              |
| Model 4 | 1          | 4                        | Temporal share with Model 1 |

Models 1, 2, and 3 are placed spatially in order, consuming all 36 columns. When Model 4 arrives it needs 4 columns but finds none free. It then searches for an existing model with a 4-column layout and finds Model 1 at columns 0–3 — a perfect match. Model 4 is placed as a temporal share on top of Model 1.

### Example 2: Single-Partition Models with Varied `tp_size` and `dp_size`

All models here are single-partition. Different `tp_size` and `dp_size` values produce different column footprints (`tp_size × dp_size × 4`), and temporal sharing groups form around each unique footprint:

| Model   | tp_size | dp_size | Columns (tp × dp × 4) | Placement                   |
| ------- | ------- | ------- | --------------------- | --------------------------- |
| Model 1 | 1       | 1       | 4                     | Spatial: 0–3                |
| Model 2 | 6       | 1       | 24                    | Spatial: 4–27               |
| Model 3 | 1       | 2       | 8                     | Spatial: 28–35              |
| Model 4 | 1       | 2       | 8                     | Temporal share with Model 3 |
| Model 5 | 6       | 1       | 24                    | Temporal share with Model 2 |
| Model 6 | 1       | 1       | 4                     | Temporal share with Model 1 |

After Models 1–3 claim the full 36 columns spatially, Models 4, 5, and 6 each find a spatial peer with the matching layout and stack as temporal shares. Notice that each unique column width (4, 24, 8) forms its own temporal-sharing group.

### What Happens When Placement Fails

Placement fails if a model's column footprint cannot be satisfied either spatially or via temporal sharing. For example: if all 36 columns are occupied by models with 4-column and 8-column layouts, a new model needing 16 columns will fail — there is no existing 16-column partition to share with.

Scheduling order matters here. A model that claims a column range early may inadvertently prevent a later model from finding a compatible temporal-share slot. If you see placement errors in a multi-model application, review the order in which sessions are created and the column footprints of each model.

---

## Step 5 — Verify Column Assignments at Runtime

Once models are running, you can observe the actual column assignments on the hardware using:

```bash
xrt-smi examine --device 0 --report aie-partitions
```

Run this in a parallel terminal while your application is active. The output shows which column ranges are currently occupied and by which partitions, letting you confirm that spatial placement and temporal sharing are happening as expected.

The example below shows the expected output for the six models in Example 2. Three spatial partitions are created — one per unique column footprint — and each partition hosts two models as temporal shares (two HW Contexts per Partition Index):

```
AIE Partitions
  Total Memory Usage: N/A
  Partition Index   : 0
    Columns: [0, 1, 2, 3]
    HW Contexts:
      |PID                 |Ctx ID     |Submissions |Migrations  |Err  |Priority |
      |Process Name        |Status     |Completions |Suspensions |     |GOPS     |
      |Memory Usage        |Instr BO   |            |            |     |FPS      |
      |                    |           |            |            |     |Latency  |
      |====================|===========|============|============|=====|=========|
      |1213                |1          |39          |0           |0    |Normal   |
      |N/A                 |Idle       |38          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
      |1213                |6          |41          |0           |0    |Normal   |
      |N/A                 |Idle       |40          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
  Partition Index   : 1
    Columns: [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27]
    HW Contexts:
      |PID                 |Ctx ID     |Submissions |Migrations  |Err  |Priority |
      |Process Name        |Status     |Completions |Suspensions |     |GOPS     |
      |Memory Usage        |Instr BO   |            |            |     |FPS      |
      |                    |           |            |            |     |Latency  |
      |====================|===========|============|============|=====|=========|
      |1213                |2          |46          |0           |0    |Normal   |
      |N/A                 |Idle       |45          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
      |1213                |5          |44          |0           |0    |Normal   |
      |N/A                 |Idle       |43          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
  Partition Index   : 2
    Columns: [28, 29, 30, 31, 32, 33, 34, 35]
    HW Contexts:
      |PID                 |Ctx ID     |Submissions |Migrations  |Err  |Priority |
      |Process Name        |Status     |Completions |Suspensions |     |GOPS     |
      |Memory Usage        |Instr BO   |            |            |     |FPS      |
      |                    |           |            |            |     |Latency  |
      |====================|===========|============|============|=====|=========|
      |1213                |3          |52          |0           |0    |Normal   |
      |N/A                 |Idle       |51          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
      |1213                |4          |50          |0           |0    |Normal   |
      |N/A                 |Idle       |49          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
```

Reading the output:

- **Partition Index 0** (columns 0–3, 4 columns) — Model 1 (`tp=1, dp=1`) placed spatially; Model 6 (`tp=1, dp=1`) stacked as a temporal share. Both share Ctx IDs 1 and 6 under the same partition.
- **Partition Index 1** (columns 4–27, 24 columns) — Model 2 (`tp=6, dp=1`) placed spatially; Model 5 (`tp=6, dp=1`) stacked as a temporal share. Ctx IDs 2 and 5.
- **Partition Index 2** (columns 28–35, 8 columns) — Model 3 (`tp=1, dp=2`) placed spatially; Model 4 (`tp=1, dp=2`) stacked as a temporal share. Ctx IDs 3 and 4.
