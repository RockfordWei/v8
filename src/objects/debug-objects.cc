// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/debug-objects.h"
#include "src/objects/debug-objects-inl.h"

namespace v8 {
namespace internal {

bool DebugInfo::IsEmpty() const { return flags() == kNone; }

bool DebugInfo::HasBreakInfo() const { return (flags() & kHasBreakInfo) != 0; }

DebugInfo::ExecutionMode DebugInfo::DebugExecutionMode() const {
  return (flags() & kDebugExecutionMode) != 0 ? kSideEffects : kBreakpoints;
}

void DebugInfo::SetDebugExecutionMode(ExecutionMode value) {
  set_flags(value == kSideEffects ? (flags() | kDebugExecutionMode)
                                  : (flags() & ~kDebugExecutionMode));
}

bool DebugInfo::ClearBreakInfo(Isolate* isolate) {
  set_debug_bytecode_array(isolate->heap()->undefined_value());
  set_break_points(isolate->heap()->empty_fixed_array());

  int new_flags = flags();
  new_flags &= ~kHasBreakInfo & ~kPreparedForDebugExecution;
  new_flags &= ~kBreakAtEntry & ~kCanBreakAtEntry;
  new_flags &= ~kDebugExecutionMode;
  set_flags(new_flags);

  return new_flags == kNone;
}

void DebugInfo::SetBreakAtEntry() {
  DCHECK(CanBreakAtEntry());
  set_flags(flags() | kBreakAtEntry);
}

void DebugInfo::ClearBreakAtEntry() {
  DCHECK(CanBreakAtEntry());
  set_flags(flags() & ~kBreakAtEntry);
}

bool DebugInfo::BreakAtEntry() const { return (flags() & kBreakAtEntry) != 0; }

bool DebugInfo::CanBreakAtEntry() const {
  return (flags() & kCanBreakAtEntry) != 0;
}

// Check if there is a break point at this source position.
bool DebugInfo::HasBreakPoint(Isolate* isolate, int source_position) {
  DCHECK(HasBreakInfo());
  // Get the break point info object for this code offset.
  Object* break_point_info = GetBreakPointInfo(isolate, source_position);

  // If there is no break point info object or no break points in the break
  // point info object there is no break point at this code offset.
  if (break_point_info->IsUndefined(isolate)) return false;
  return BreakPointInfo::cast(break_point_info)->GetBreakPointCount(isolate) >
         0;
}

// Get the break point info object for this source position.
Object* DebugInfo::GetBreakPointInfo(Isolate* isolate, int source_position) {
  DCHECK(HasBreakInfo());
  for (int i = 0; i < break_points()->length(); i++) {
    if (!break_points()->get(i)->IsUndefined(isolate)) {
      BreakPointInfo* break_point_info =
          BreakPointInfo::cast(break_points()->get(i));
      if (break_point_info->source_position() == source_position) {
        return break_point_info;
      }
    }
  }
  return isolate->heap()->undefined_value();
}

bool DebugInfo::ClearBreakPoint(Isolate* isolate, Handle<DebugInfo> debug_info,
                                Handle<BreakPoint> break_point) {
  DCHECK(debug_info->HasBreakInfo());
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (debug_info->break_points()->get(i)->IsUndefined(isolate)) continue;
    Handle<BreakPointInfo> break_point_info = Handle<BreakPointInfo>(
        BreakPointInfo::cast(debug_info->break_points()->get(i)), isolate);
    if (BreakPointInfo::HasBreakPoint(isolate, break_point_info, break_point)) {
      BreakPointInfo::ClearBreakPoint(isolate, break_point_info, break_point);
      return true;
    }
  }
  return false;
}

void DebugInfo::SetBreakPoint(Isolate* isolate, Handle<DebugInfo> debug_info,
                              int source_position,
                              Handle<BreakPoint> break_point) {
  DCHECK(debug_info->HasBreakInfo());
  Handle<Object> break_point_info(
      debug_info->GetBreakPointInfo(isolate, source_position), isolate);
  if (!break_point_info->IsUndefined(isolate)) {
    BreakPointInfo::SetBreakPoint(
        isolate, Handle<BreakPointInfo>::cast(break_point_info), break_point);
    return;
  }

  // Adding a new break point for a code offset which did not have any
  // break points before. Try to find a free slot.
  static const int kNoBreakPointInfo = -1;
  int index = kNoBreakPointInfo;
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (debug_info->break_points()->get(i)->IsUndefined(isolate)) {
      index = i;
      break;
    }
  }
  if (index == kNoBreakPointInfo) {
    // No free slot - extend break point info array.
    Handle<FixedArray> old_break_points =
        Handle<FixedArray>(debug_info->break_points(), isolate);
    Handle<FixedArray> new_break_points = isolate->factory()->NewFixedArray(
        old_break_points->length() +
        DebugInfo::kEstimatedNofBreakPointsInFunction);

    debug_info->set_break_points(*new_break_points);
    for (int i = 0; i < old_break_points->length(); i++) {
      new_break_points->set(i, old_break_points->get(i));
    }
    index = old_break_points->length();
  }
  DCHECK_NE(index, kNoBreakPointInfo);

  // Allocate new BreakPointInfo object and set the break point.
  Handle<BreakPointInfo> new_break_point_info =
      isolate->factory()->NewBreakPointInfo(source_position);
  BreakPointInfo::SetBreakPoint(isolate, new_break_point_info, break_point);
  debug_info->break_points()->set(index, *new_break_point_info);
}

// Get the break point objects for a source position.
Handle<Object> DebugInfo::GetBreakPoints(Isolate* isolate,
                                         int source_position) {
  DCHECK(HasBreakInfo());
  Object* break_point_info = GetBreakPointInfo(isolate, source_position);
  if (break_point_info->IsUndefined(isolate)) {
    return isolate->factory()->undefined_value();
  }
  return Handle<Object>(BreakPointInfo::cast(break_point_info)->break_points(),
                        isolate);
}

// Get the total number of break points.
int DebugInfo::GetBreakPointCount(Isolate* isolate) {
  DCHECK(HasBreakInfo());
  int count = 0;
  for (int i = 0; i < break_points()->length(); i++) {
    if (!break_points()->get(i)->IsUndefined(isolate)) {
      BreakPointInfo* break_point_info =
          BreakPointInfo::cast(break_points()->get(i));
      count += break_point_info->GetBreakPointCount(isolate);
    }
  }
  return count;
}

Handle<Object> DebugInfo::FindBreakPointInfo(Isolate* isolate,
                                             Handle<DebugInfo> debug_info,
                                             Handle<BreakPoint> break_point) {
  DCHECK(debug_info->HasBreakInfo());
  for (int i = 0; i < debug_info->break_points()->length(); i++) {
    if (!debug_info->break_points()->get(i)->IsUndefined(isolate)) {
      Handle<BreakPointInfo> break_point_info = Handle<BreakPointInfo>(
          BreakPointInfo::cast(debug_info->break_points()->get(i)), isolate);
      if (BreakPointInfo::HasBreakPoint(isolate, break_point_info,
                                        break_point)) {
        return break_point_info;
      }
    }
  }
  return isolate->factory()->undefined_value();
}

bool DebugInfo::HasCoverageInfo() const {
  return (flags() & kHasCoverageInfo) != 0;
}

bool DebugInfo::ClearCoverageInfo(Isolate* isolate) {
  if (HasCoverageInfo()) {
    set_coverage_info(isolate->heap()->undefined_value());

    int new_flags = flags() & ~kHasCoverageInfo;
    set_flags(new_flags);
  }
  return flags() == kNone;
}

namespace {
bool IsEqual(BreakPoint* break_point1, BreakPoint* break_point2) {
  return break_point1->id() == break_point2->id();
}
}  // namespace

// Remove the specified break point object.
void BreakPointInfo::ClearBreakPoint(Isolate* isolate,
                                     Handle<BreakPointInfo> break_point_info,
                                     Handle<BreakPoint> break_point) {
  // If there are no break points just ignore.
  if (break_point_info->break_points()->IsUndefined(isolate)) return;
  // If there is a single break point clear it if it is the same.
  if (!break_point_info->break_points()->IsFixedArray()) {
    if (IsEqual(BreakPoint::cast(break_point_info->break_points()),
                *break_point)) {
      break_point_info->set_break_points(isolate->heap()->undefined_value());
    }
    return;
  }
  // If there are multiple break points shrink the array
  DCHECK(break_point_info->break_points()->IsFixedArray());
  Handle<FixedArray> old_array =
      Handle<FixedArray>(FixedArray::cast(break_point_info->break_points()));
  Handle<FixedArray> new_array =
      isolate->factory()->NewFixedArray(old_array->length() - 1);
  int found_count = 0;
  for (int i = 0; i < old_array->length(); i++) {
    if (IsEqual(BreakPoint::cast(old_array->get(i)), *break_point)) {
      DCHECK_EQ(found_count, 0);
      found_count++;
    } else {
      new_array->set(i - found_count, old_array->get(i));
    }
  }
  // If the break point was found in the list change it.
  if (found_count > 0) break_point_info->set_break_points(*new_array);
}

// Add the specified break point object.
void BreakPointInfo::SetBreakPoint(Isolate* isolate,
                                   Handle<BreakPointInfo> break_point_info,
                                   Handle<BreakPoint> break_point) {
  // If there was no break point objects before just set it.
  if (break_point_info->break_points()->IsUndefined(isolate)) {
    break_point_info->set_break_points(*break_point);
    return;
  }
  // If the break point object is the same as before just ignore.
  if (break_point_info->break_points() == *break_point) return;
  // If there was one break point object before replace with array.
  if (!break_point_info->break_points()->IsFixedArray()) {
    Handle<FixedArray> array = isolate->factory()->NewFixedArray(2);
    array->set(0, break_point_info->break_points());
    array->set(1, *break_point);
    break_point_info->set_break_points(*array);
    return;
  }
  // If there was more than one break point before extend array.
  Handle<FixedArray> old_array =
      Handle<FixedArray>(FixedArray::cast(break_point_info->break_points()));
  Handle<FixedArray> new_array =
      isolate->factory()->NewFixedArray(old_array->length() + 1);
  for (int i = 0; i < old_array->length(); i++) {
    // If the break point was there before just ignore.
    if (IsEqual(BreakPoint::cast(old_array->get(i)), *break_point)) return;
    new_array->set(i, old_array->get(i));
  }
  // Add the new break point.
  new_array->set(old_array->length(), *break_point);
  break_point_info->set_break_points(*new_array);
}

bool BreakPointInfo::HasBreakPoint(Isolate* isolate,
                                   Handle<BreakPointInfo> break_point_info,
                                   Handle<BreakPoint> break_point) {
  // No break point.
  if (break_point_info->break_points()->IsUndefined(isolate)) {
    return false;
  }
  // Single break point.
  if (!break_point_info->break_points()->IsFixedArray()) {
    return IsEqual(BreakPoint::cast(break_point_info->break_points()),
                   *break_point);
  }
  // Multiple break points.
  FixedArray* array = FixedArray::cast(break_point_info->break_points());
  for (int i = 0; i < array->length(); i++) {
    if (IsEqual(BreakPoint::cast(array->get(i)), *break_point)) {
      return true;
    }
  }
  return false;
}

// Get the number of break points.
int BreakPointInfo::GetBreakPointCount(Isolate* isolate) {
  // No break point.
  if (break_points()->IsUndefined(isolate)) return 0;
  // Single break point.
  if (!break_points()->IsFixedArray()) return 1;
  // Multiple break points.
  return FixedArray::cast(break_points())->length();
}

int CoverageInfo::SlotCount() const {
  DCHECK_EQ(kFirstSlotIndex, length() % kSlotIndexCount);
  return (length() - kFirstSlotIndex) / kSlotIndexCount;
}

int CoverageInfo::StartSourcePosition(int slot_index) const {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  return Smi::ToInt(get(slot_start + kSlotStartSourcePositionIndex));
}

int CoverageInfo::EndSourcePosition(int slot_index) const {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  return Smi::ToInt(get(slot_start + kSlotEndSourcePositionIndex));
}

int CoverageInfo::BlockCount(int slot_index) const {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  return Smi::ToInt(get(slot_start + kSlotBlockCountIndex));
}

void CoverageInfo::InitializeSlot(int slot_index, int from_pos, int to_pos) {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  set(slot_start + kSlotStartSourcePositionIndex, Smi::FromInt(from_pos));
  set(slot_start + kSlotEndSourcePositionIndex, Smi::FromInt(to_pos));
  set(slot_start + kSlotBlockCountIndex, Smi::kZero);
}

void CoverageInfo::IncrementBlockCount(int slot_index) {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  const int old_count = BlockCount(slot_index);
  set(slot_start + kSlotBlockCountIndex, Smi::FromInt(old_count + 1));
}

void CoverageInfo::ResetBlockCount(int slot_index) {
  DCHECK_LT(slot_index, SlotCount());
  const int slot_start = CoverageInfo::FirstIndexForSlot(slot_index);
  set(slot_start + kSlotBlockCountIndex, Smi::kZero);
}

void CoverageInfo::Print(std::unique_ptr<char[]> function_name) {
  DCHECK(FLAG_trace_block_coverage);
  DisallowHeapAllocation no_gc;

  StdoutStream os;
  os << "Coverage info (";
  if (strlen(function_name.get()) > 0) {
    os << function_name.get();
  } else {
    os << "{anonymous}";
  }
  os << "):" << std::endl;

  for (int i = 0; i < SlotCount(); i++) {
    os << "{" << StartSourcePosition(i) << "," << EndSourcePosition(i) << "}"
       << std::endl;
  }
}

}  // namespace internal
}  // namespace v8
