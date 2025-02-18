// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Array.prototype.top = function() {
  if (this.length == 0) return undefined;
  return this[this.length - 1];
}


function PlotScriptComposer(kResX, kResY) {
  // Constants.
  var kV8BinarySuffixes = ["/d8", "/libv8.so"];
  var kStackFrames = 8;             // Stack frames to display in the plot.

  var kTimerEventWidth = 0.33;      // Width of each timeline.
  var kExecutionFrameWidth = 0.2;   // Width of the top stack frame line.
  var kStackFrameWidth = 0.1;       // Width of the lower stack frame lines.
  var kGapWidth = 0.05;             // Gap between stack frame lines.

  var kY1Offset = 10;               // Offset for stack frame vs. event lines.
  var kPauseLabelPadding = 5;       // Padding for pause time labels.
  var kNumPauseLabels = 7;          // Number of biggest pauses to label.
  var kCodeKindLabelPadding = 100;  // Padding for code kind labels.

  var kTickHalfDuration = 0.5;      // Duration of half a tick in ms.
  var kMinRangeLength = 0.0005;     // Minimum length for an event in ms.

  var kNumThreads = 2;              // Number of threads.
  var kExecutionThreadId = 0;       // ID of main thread.

  // Data structures.
  function TimerEvent(label, color, pause, thread_id) {
    assert(thread_id >= 0 && thread_id < kNumThreads, "invalid thread id");
    this.label = label;
    this.color = color;
    this.pause = pause;
    this.ranges = [];
    this.thread_id = thread_id;
    this.index = ++num_timer_event;
  }

  function CodeKind(color, kinds) {
    this.color = color;
    this.in_execution = [];
    this.stack_frames = [];
    for (var i = 0; i < kStackFrames; i++) this.stack_frames.push([]);
    this.kinds = kinds;
  }

  function Range(start, end) {
    // Everthing here are in milliseconds.
    this.start = start;
    this.end = end;
  }

  Range.prototype.duration = function() { return this.end - this.start; }

  function Tick(tick) {
    this.tick = tick;
  }

  // Init values.
  var num_timer_event = kY1Offset + 0.5;

  var TimerEvents = {
      'V8.Execute':
        new TimerEvent("execution", "#000000", false, 0),
      'V8.External':
        new TimerEvent("external", "#3399FF", false, 0),
      'V8.CompileFullCode':
        new TimerEvent("compile unopt", "#CC0000",  true, 0),
      'V8.RecompileSynchronous':
        new TimerEvent("recompile sync", "#CC0044",  true, 0),
      'V8.RecompileParallel':
        new TimerEvent("recompile async", "#CC4499", false, 1),
      'V8.CompileEval':
        new TimerEvent("compile eval", "#CC4400",  true, 0),
      'V8.Parse':
        new TimerEvent("parse", "#00CC00",  true, 0),
      'V8.PreParse':
        new TimerEvent("preparse", "#44CC00",  true, 0),
      'V8.ParseLazy':
        new TimerEvent("lazy parse", "#00CC44",  true, 0),
      'V8.GCScavenger':
        new TimerEvent("gc scavenge", "#0044CC",  true, 0),
      'V8.GCCompactor':
        new TimerEvent("gc compaction", "#4444CC",  true, 0),
      'V8.GCContext':
        new TimerEvent("gc context", "#4400CC",  true, 0),
  };

  var CodeKinds = {
      'external ': new CodeKind("#3399FF", [-2]),
      'runtime  ': new CodeKind("#000000", [-1]),
      'full code': new CodeKind("#DD0000", [0]),
      'opt code ': new CodeKind("#00EE00", [1]),
      'code stub': new CodeKind("#FF00FF", [2]),
      'built-in ': new CodeKind("#AA00AA", [3]),
      'inl.cache': new CodeKind("#4444AA",
                                [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]),
      'reg.exp. ': new CodeKind("#0000FF", [15]),
  };

  var code_map = new CodeMap();
  var execution_pauses = [];
  var event_stack = [];
  var last_time_stamp = [];
  for (var i = 0; i < kNumThreads; i++) {
    event_stack[i] = [];
    last_time_stamp[i] = -1;
  }

  var range_start = undefined;
  var range_end = undefined;
  var obj_index = 0;
  var pause_tolerance = 0.005;  // Milliseconds.
  var distortion = 0;

  // Utility functions.
  function assert(something, message) {
    if (!something) print(new Error(message).stack);
  }

  function FindCodeKind(kind) {
    for (name in CodeKinds) {
      if (CodeKinds[name].kinds.indexOf(kind) >= 0) {
        return CodeKinds[name];
      }
    }
  }

  function TicksToRanges(ticks) {
    var ranges = [];
    for (var i = 0; i < ticks.length; i++) {
      var tick = ticks[i].tick;
      ranges.push(
          new Range(tick - kTickHalfDuration, tick + kTickHalfDuration));
    }
    return ranges;
  }

  function MergeRanges(ranges) {
    ranges.sort(function(a, b) { return a.start - b.start; });
    var result = [];
    var j = 0;
    for (var i = 0; i < ranges.length; i = j) {
      var merge_start = ranges[i].start;
      if (merge_start > range_end) break;  // Out of plot range.
      var merge_end = ranges[i].end;
      for (j = i + 1; j < ranges.length; j++) {
        var next_range = ranges[j];
        // Don't merge ranges if there is no overlap (incl. merge tolerance).
        if (next_range.start > merge_end + pause_tolerance) break;
        // Merge ranges.
        if (next_range.end > merge_end) {  // Extend range end.
          merge_end = next_range.end;
        }
      }
      if (merge_end < range_start) continue;  // Out of plot range.
      if (merge_end < merge_start) continue;  // Not an actual range.
      result.push(new Range(merge_start, merge_end));
    }
    return result;
  }

  function RestrictRangesTo(ranges, start, end) {
    var result = [];
    for (var i = 0; i < ranges.length; i++) {
      if (ranges[i].start <= end && ranges[i].end >= start) {
        result.push(new Range(Math.max(ranges[i].start, start),
                              Math.min(ranges[i].end, end)));
      }
    }
    return result;
  }

  // Public methods.
  this.collectData = function(input, distortion_per_entry) {

    // Parse functions.
    var parseTimeStamp = function(timestamp) {
      distortion += distortion_per_entry;
      return parseInt(timestamp) / 1000 - distortion;
    }

    var processTimerEventStart = function(name, start) {
      // Find out the thread id.
      var new_event = TimerEvents[name];
      if (new_event === undefined) return;
      var thread_id = new_event.thread_id;

      start = Math.max(last_time_stamp[thread_id] + kMinRangeLength, start);

      // Last event on this thread is done with the start of this event.
      var last_event = event_stack[thread_id].top();
      if (last_event !== undefined) {
        var new_range = new Range(last_time_stamp[thread_id], start);
        last_event.ranges.push(new_range);
      }
      event_stack[thread_id].push(new_event);
      last_time_stamp[thread_id] = start;
    };

    var processTimerEventEnd = function(name, end) {
      // Find out about the thread_id.
      var finished_event = TimerEvents[name];
      var thread_id = finished_event.thread_id;
      assert(finished_event === event_stack[thread_id].pop(),
             "inconsistent event stack");

      end = Math.max(last_time_stamp[thread_id] + kMinRangeLength, end);

      var new_range = new Range(last_time_stamp[thread_id], end);
      finished_event.ranges.push(new_range);
      last_time_stamp[thread_id] = end;
    };

    var processCodeCreateEvent = function(type, kind, address, size, name) {
      var code_entry = new CodeMap.CodeEntry(size, name);
      code_entry.kind = kind;
      code_map.addCode(address, code_entry);
    };

    var processCodeMoveEvent = function(from, to) {
      code_map.moveCode(from, to);
    };

    var processCodeDeleteEvent = function(address) {
      code_map.deleteCode(address);
    };

    var processSharedLibrary = function(name, start, end) {
      var code_entry = new CodeMap.CodeEntry(end - start, name);
      code_entry.kind = -2;  // External code kind.
      for (var i = 0; i < kV8BinarySuffixes.length; i++) {
        var suffix = kV8BinarySuffixes[i];
        if (name.indexOf(suffix, name.length - suffix.length) >= 0) {
          code_entry.kind = -1;  // V8 runtime code kind.
          break;
        }
      }
      code_map.addLibrary(start, code_entry);
    };

    var processTimerEventStart = function(name, start) {
      // Find out the thread id.
      var new_event = TimerEvents[name];
      if (new_event === undefined) return;
      var thread_id = new_event.thread_id;

      start = Math.max(last_time_stamp[thread_id] + kMinRangeLength, start);

      // Last event on this thread is done with the start of this event.
      var last_event = event_stack[thread_id].top();
      if (last_event !== undefined) {
        var new_range = new Range(last_time_stamp[thread_id], start);
        last_event.ranges.push(new_range);
      }
      event_stack[thread_id].push(new_event);
      last_time_stamp[thread_id] = start;
    };

    var processTimerEventEnd = function(name, end) {
      // Find out about the thread_id.
      var finished_event = TimerEvents[name];
      var thread_id = finished_event.thread_id;
      assert(finished_event === event_stack[thread_id].pop(),
             "inconsistent event stack");

      end = Math.max(last_time_stamp[thread_id] + kMinRangeLength, end);

      var new_range = new Range(last_time_stamp[thread_id], end);
      finished_event.ranges.push(new_range);
      last_time_stamp[thread_id] = end;
    };

    var processCodeCreateEvent = function(type, kind, address, size, name) {
      var code_entry = new CodeMap.CodeEntry(size, name);
      code_entry.kind = kind;
      code_map.addCode(address, code_entry);
    };

    var processCodeMoveEvent = function(from, to) {
      code_map.moveCode(from, to);
    };

    var processCodeDeleteEvent = function(address) {
      code_map.deleteCode(address);
    };

    var processSharedLibrary = function(name, start, end) {
      var code_entry = new CodeMap.CodeEntry(end - start, name);
      code_entry.kind = -3;  // External code kind.
      for (var i = 0; i < kV8BinarySuffixes.length; i++) {
        var suffix = kV8BinarySuffixes[i];
        if (name.indexOf(suffix, name.length - suffix.length) >= 0) {
          code_entry.kind = -1;  // V8 runtime code kind.
          break;
        }
      }
      code_map.addLibrary(start, code_entry);
    };

    var processTickEvent = function(
        pc, sp, timer, unused_x, unused_y, vmstate, stack) {
      var tick = new Tick(timer);

      var entry = code_map.findEntry(pc);
      if (entry) FindCodeKind(entry.kind).in_execution.push(tick);

      for (var i = 0; i < kStackFrames; i++) {
        if (!stack[i]) break;
        var entry = code_map.findEntry(stack[i]);
        if (entry) FindCodeKind(entry.kind).stack_frames[i].push(tick);
      }
    };
    // Collect data from log.
    var logreader = new LogReader(
      { 'timer-event-start': { parsers: [null, parseTimeStamp],
                               processor: processTimerEventStart },
        'timer-event-end':   { parsers: [null, parseTimeStamp],
                               processor: processTimerEventEnd },
        'shared-library': { parsers: [null, parseInt, parseInt],
                            processor: processSharedLibrary },
        'code-creation':  { parsers: [null, parseInt, parseInt, parseInt, null],
                            processor: processCodeCreateEvent },
        'code-move':      { parsers: [parseInt, parseInt],
                            processor: processCodeMoveEvent },
        'code-delete':    { parsers: [parseInt],
                            processor: processCodeDeleteEvent },
        'tick':           { parsers: [parseInt, parseInt, parseTimeStamp,
                                      null, null, parseInt, 'var-args'],
                            processor: processTickEvent }
      });

    var line;
    while (line = input()) {
      logreader.processLogLine(line);
    }

    // Collect execution pauses.
    for (name in TimerEvents) {
      var event = TimerEvents[name];
      if (!event.pause) continue;
      var ranges = event.ranges;
      for (var j = 0; j < ranges.length; j++) execution_pauses.push(ranges[j]);
    }
    execution_pauses = MergeRanges(execution_pauses);
  };


  this.findPlotRange = function(
    range_start_override, range_end_override, result_callback) {
    var start_found = (range_start_override || range_start_override == 0);
    var end_found = (range_end_override || range_end_override == 0);
    range_start = start_found ? range_start_override : Infinity;
    range_end = end_found ? range_end_override : -Infinity;

    if (!start_found || !end_found) {
      for (name in TimerEvents) {
        var ranges = TimerEvents[name].ranges;
        for (var i = 0; i < ranges.length; i++) {
          if (ranges[i].start < range_start && !start_found) {
            range_start = ranges[i].start;
          }
          if (ranges[i].end > range_end && !end_found) {
            range_end = ranges[i].end;
          }
        }
      }

      for (codekind in CodeKinds) {
        var ticks = CodeKinds[codekind].in_execution;
        for (var i = 0; i < ticks.length; i++) {
          if (ticks[i].tick < range_start && !start_found) {
            range_start = ticks[i].tick;
          }
          if (ticks[i].tick > range_end && !end_found) {
            range_end = ticks[i].tick;
          }
        }
      }
    }
    // Set pause tolerance to something appropriate for the plot resolution
    // to make it easier for gnuplot.
    pause_tolerance = (range_end - range_start) / kResX / 10;

    if (typeof result_callback === 'function') {
      result_callback(range_start, range_end);
    }
  };


  this.assembleOutput = function(output) {
    output("set yrange [0:" + (num_timer_event + 1) + "]");
    output("set xlabel \"execution time in ms\"");
    output("set xrange [" + range_start + ":" + range_end + "]");
    output("set style fill pattern 2 bo 1");
    output("set style rect fs solid 1 noborder");
    output("set style line 1 lt 1 lw 1 lc rgb \"#000000\"");
    output("set xtics out nomirror");
    output("unset key");

    function DrawBar(row, color, start, end, width) {
      obj_index++;
      command = "set object " + obj_index + " rect";
      command += " from " + start + ", " + (row - width);
      command += " to " + end + ", " + (row + width);
      command += " fc rgb \"" + color + "\"";
      output(command);
    }

    var percentages = {};
    var total = 0;
    for (var name in TimerEvents) {
      var event = TimerEvents[name];
      var ranges = RestrictRangesTo(event.ranges, range_start, range_end);
      ranges = MergeRanges(ranges);
      var sum =
        ranges.map(function(range) { return range.duration(); })
            .reduce(function(a, b) { return a + b; }, 0);
      percentages[name] = (sum / (range_end - range_start) * 100).toFixed(1);
    }

    // Name Y-axis.
    var ytics = [];
    for (name in TimerEvents) {
      var index = TimerEvents[name].index;
      var label = TimerEvents[name].label;
      ytics.push('"' + label + ' (' + percentages[name] + '%%)" ' + index);
    }
    ytics.push('"code kind color coding" ' + kY1Offset);
    ytics.push('"code kind in execution" ' + (kY1Offset - 1));
    ytics.push('"top ' + kStackFrames + ' js stack frames"' + ' ' +
               (kY1Offset - 2));
    ytics.push('"pause times" 0');
    output("set ytics out nomirror (" + ytics.join(', ') + ")");

    // Plot timeline.
    for (var name in TimerEvents) {
      var event = TimerEvents[name];
      var ranges = MergeRanges(event.ranges);
      for (var i = 0; i < ranges.length; i++) {
        DrawBar(event.index, event.color,
                ranges[i].start, ranges[i].end,
                kTimerEventWidth);
      }
    }

    // Plot code kind gathered from ticks.
    for (var name in CodeKinds) {
      var code_kind = CodeKinds[name];
      var offset = kY1Offset - 1;
      // Top most frame.
      var row = MergeRanges(TicksToRanges(code_kind.in_execution));
      for (var j = 0; j < row.length; j++) {
        DrawBar(offset, code_kind.color,
                row[j].start, row[j].end, kExecutionFrameWidth);
      }
      offset = offset - 2 * kExecutionFrameWidth - kGapWidth;
      // Javascript frames.
      for (var i = 0; i < kStackFrames; i++) {
        offset = offset - 2 * kStackFrameWidth - kGapWidth;
        row = MergeRanges(TicksToRanges(code_kind.stack_frames[i]));
        for (var j = 0; j < row.length; j++) {
          DrawBar(offset, code_kind.color,
                  row[j].start, row[j].end, kStackFrameWidth);
        }
      }
    }

    // Add labels as legend for code kind colors.
    var padding = kCodeKindLabelPadding * (range_end - range_start) / kResX;
    var label_x = range_start;
    var label_y = kY1Offset;
    for (var name in CodeKinds) {
      label_x += padding;
      output("set label \"" + name + "\" at " + label_x + "," + label_y +
             " textcolor rgb \"" + CodeKinds[name].color + "\"" +
             " font \"Helvetica,9'\"");
      obj_index++;
    }

    if (execution_pauses.length == 0) {
      // Force plot and return without plotting execution pause impulses.
      output("plot 1/0");
      return;
    }

    // Label the longest pauses.
    execution_pauses.sort(
        function(a, b) { return b.duration() - a.duration(); });

    var max_pause_time = execution_pauses[0].duration();
    padding = kPauseLabelPadding * (range_end - range_start) / kResX;
    var y_scale = kY1Offset / max_pause_time / 2;
    for (var i = 0; i < execution_pauses.length && i < kNumPauseLabels; i++) {
      var pause = execution_pauses[i];
      var label_content = (pause.duration() | 0) + " ms";
      var label_x = pause.end + padding;
      var label_y = Math.max(1, (pause.duration() * y_scale));
      output("set label \"" + label_content + "\" at " +
             label_x + "," + label_y + " font \"Helvetica,7'\"");
      obj_index++;
    }

    // Scale second Y-axis appropriately.
    var y2range = max_pause_time * num_timer_event / kY1Offset * 2;
    output("set y2range [0:" + y2range + "]");
    // Plot graph with impulses as data set.
    output("plot '-' using 1:2 axes x1y2 with impulses ls 1");
    for (var i = 0; i < execution_pauses.length; i++) {
      var pause = execution_pauses[i];
      output(pause.end + " " + pause.duration());
      obj_index++;
    }
    output("e");
    return obj_index;
  };
}
