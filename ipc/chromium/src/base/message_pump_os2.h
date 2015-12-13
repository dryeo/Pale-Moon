// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_PUMP_OS2_H_
#define BASE_MESSAGE_PUMP_OS2_H_

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "base/lock.h"
#include "base/message_pump.h"
#include "base/observer_list.h"
#include "base/scoped_handle.h"
#include "base/time.h"

#include "base/hash_tables.h"

namespace base {

// NOTE: This code is a copy-paste from message_pump_win.* with OS/2 specific
// modifications. It may make sense to keep them in sync.

// MessagePumpOS2 serves as the base for specialized versions of the MessagePump
// for Windows. It provides basic functionality like handling of observers and
// controlling the lifetime of the message pump.
class MessagePumpOS2 : public MessagePump {
 public:
  // An Observer is an object that receives global notifications from the
  // MessageLoop.
  //
  // NOTE: An Observer implementation should be extremely fast!
  //
  class Observer {
   public:
    virtual ~Observer() {}

    // This method is called before processing a message.
    // The message may be undefined in which case msg.message is 0
    virtual void WillProcessMessage(const QMSG& msg) = 0;

    // This method is called when control returns from processing a UI message.
    // The message may be undefined in which case msg.message is 0
    virtual void DidProcessMessage(const QMSG& msg) = 0;
  };

  // Dispatcher is used during a nested invocation of Run to dispatch events.
  // If Run is invoked with a non-NULL Dispatcher, MessageLoop does not
  // dispatch events (or invoke TranslateMessage), rather every message is
  // passed to Dispatcher's Dispatch method for dispatch. It is up to the
  // Dispatcher to dispatch, or not, the event.
  //
  // The nested loop is exited by either posting a quit, or returning false
  // from Dispatch.
  class Dispatcher {
   public:
    virtual ~Dispatcher() {}
    // Dispatches the event. If true is returned processing continues as
    // normal. If false is returned, the nested loop exits immediately.
    virtual bool Dispatch(const QMSG& msg) = 0;
  };

  MessagePumpOS2() : have_work_(0), state_(NULL) {}
  virtual ~MessagePumpOS2() {}

  // Add an Observer, which will start receiving notifications immediately.
  void AddObserver(Observer* observer);

  // Remove an Observer.  It is safe to call this method while an Observer is
  // receiving a notification callback.
  void RemoveObserver(Observer* observer);

  // Give a chance to code processing additional messages to notify the
  // message loop observers that another message has been processed.
  void WillProcessMessage(const QMSG& msg);
  void DidProcessMessage(const QMSG& msg);

  // Like MessagePump::Run, but QMSG objects are routed through dispatcher.
  void RunWithDispatcher(Delegate* delegate, Dispatcher* dispatcher);

  // MessagePump methods:
  virtual void Run(Delegate* delegate) { RunWithDispatcher(delegate, NULL); }
  virtual void Quit();

 protected:
  struct RunState {
    Delegate* delegate;
    Dispatcher* dispatcher;

    // Used to flag that the current Run() invocation should return ASAP.
    bool should_quit;

    // Used to count how many Run() invocations are on the stack.
    int run_depth;
  };

  virtual void DoRunLoop() = 0;
  int GetCurrentDelay() const;

  ObserverList<Observer> observers_;

  // The time at which delayed work should run.
  TimeTicks delayed_work_time_;

  // A boolean value used to indicate if there is a kMsgDoWork message pending
  // in the Windows Message queue.  There is at most one such message, and it
  // can drive execution of tasks when a native message pump is running.
  volatile unsigned int have_work_;

  // State for the current invocation of Run.
  RunState* state_;
};

//-----------------------------------------------------------------------------
// MessagePumpForUI extends MessagePumpOS2 with methods that are particular to a
// MessageLoop instantiated with TYPE_UI.
//
// MessagePumpForUI implements a "traditional" Windows message pump. It contains
// a nearly infinite loop that peeks out messages, and then dispatches them.
// Intermixed with those peeks are callouts to DoWork for pending tasks, and
// DoDelayedWork for pending timers. When there are no events to be serviced,
// this pump goes into a wait state. In most cases, this message pump handles
// all processing.
//
// However, when a task, or windows event, invokes on the stack a native dialog
// box or such, that window typically provides a bare bones (native?) message
// pump.  That bare-bones message pump generally supports little more than a
// peek of the Windows message queue, followed by a dispatch of the peeked
// message.  MessageLoop extends that bare-bones message pump to also service
// Tasks, at the cost of some complexity.
//
// The basic structure of the extension (refered to as a sub-pump) is that a
// special message, kMsgHaveWork, is repeatedly injected into the Windows
// Message queue.  Each time the kMsgHaveWork message is peeked, checks are
// made for an extended set of events, including the availability of Tasks to
// run.
//
// After running a task, the special message kMsgHaveWork is again posted to
// the Windows Message queue, ensuring a future time slice for processing a
// future event.  To prevent flooding the Windows Message queue, care is taken
// to be sure that at most one kMsgHaveWork message is EVER pending in the
// Window's Message queue.
//
// There are a few additional complexities in this system where, when there are
// no Tasks to run, this otherwise infinite stream of messages which drives the
// sub-pump is halted.  The pump is automatically re-started when Tasks are
// queued.
//
// A second complexity is that the presence of this stream of posted tasks may
// prevent a bare-bones message pump from ever peeking a WM_PAINT or WM_TIMER.
// Such paint and timer events always give priority to a posted message, such as
// kMsgHaveWork messages.  As a result, care is taken to do some peeking in
// between the posting of each kMsgHaveWork message (i.e., after kMsgHaveWork
// is peeked, and before a replacement kMsgHaveWork is posted).
//
// NOTE: Although it may seem odd that messages are used to start and stop this
// flow (as opposed to signaling objects, etc.), it should be understood that
// the native message pump will *only* respond to messages.  As a result, it is
// an excellent choice.  It is also helpful that the starter messages that are
// placed in the queue when new task arrive also awakens DoRunLoop.
//
class MessagePumpForUI : public MessagePumpOS2 {
 public:
  struct TimerID {
    HWND hwnd; USHORT id;
    bool operator==(const TimerID &other) const {
      return memcmp(this, &other, sizeof(TimerID)) == 0;
    }
  };

  MessagePumpForUI();
  virtual ~MessagePumpForUI();

  // MessagePump methods:
  virtual void ScheduleWork();
  virtual void ScheduleDelayedWork(const TimeTicks& delayed_work_time);

  // Applications can call this to encourage us to process all pending WM_PAINT
  // messages.  This method will process all paint messages the Windows Message
  // queue can provide, up to some fixed number (to avoid any infinite loops).
  void PumpOutPendingPaintMessages();

 private:
  static ULONG SetTimer(HWND hwnd, MessagePumpForUI *that, int msec);
  static MessagePumpForUI *GetTimer(HWND hwnd, ULONG id);
  static void KillTimer(HWND hwnd, ULONG id);
  static MRESULT APIENTRY WndProcThunk(
      HWND hwnd, ULONG message, MPARAM mp1, MPARAM mp2);
  virtual void DoRunLoop();
  void InitMessageWnd();
  void WaitForWork();
  void HandleWorkMessage();
  void HandleTimerMessage();
  bool ProcessNextWindowsMessage();
  bool ProcessMessageHelper(QMSG& msg);
  bool ProcessPumpReplacementMessage();

  // A hidden message-only window.
  HMQ hmq_;
  HWND message_hwnd_;
  ULONG timer_id_;

  // Timer ID -> MessagePumpForUI map
  static Lock timer_ids_lock_;
  typedef base::hash_map<TimerID, MessagePumpForUI *> TimerIDMap;
  static TimerIDMap timer_ids_;
};

// NOTE: For MessagePumpForIO we use MessagePumpLibevent on OS/2

}  // namespace base

#endif  // BASE_MESSAGE_PUMP_OS2_H_
