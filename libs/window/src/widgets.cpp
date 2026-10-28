#include "hackos/gui.h"
#include "hackos/user.h"
#include "hackos/stdio.h"

static Gui::View widgetView;
static Gui::Window widgetWindow;
static const char* const examples[] = {
    "Clipped drawing",           "PSF text",        "Rows and columns", "Flexible layout",     "Buttons",
    "Disabled controls",         "Checkboxes",      "Text selection",   "Clipboard copy",      "Clipboard paste",
    "Horizontal text scrolling", "Scrolling lists", "Arrow navigation", "Page Up / Page Down", "Home / End",
    "Tab / Shift-Tab",           "Pointer capture", "Focus recovery",   "Modal dialogs",       "Atomic presentation"};

int WidgetDemo(bool trace)
{
    widgetView.Init();
    widgetView.SetLayout(0, 0, 0, 0, 12, 8);
    unsigned title = widgetView.Add(Gui::Kind::Label, 0, "HackOS GUI library - keyboard and pointer widgets");
    unsigned toolbar = widgetView.Add(Gui::Kind::Row, 0);
    widgetView.SetLayout(toolbar, 0, 32, 0, 0, 8);
    unsigned apply = widgetView.Add(Gui::Kind::Button, toolbar, "Apply");
    unsigned unavailable = widgetView.Add(Gui::Kind::Button, toolbar, "Disabled");
    unsigned about = widgetView.Add(Gui::Kind::Button, toolbar, "About");
    widgetView.SetLayout(apply, 0, 32, 1);
    widgetView.SetLayout(unavailable, 0, 32, 1);
    widgetView.SetLayout(about, 0, 32, 1);
    widgetView.SetEnabled(unavailable, false);
    unsigned field = widgetView.Add(Gui::Kind::TextField, 0, "Hello GUI");
    unsigned check = widgetView.Add(Gui::Kind::CheckBox, 0, "Enable preview");
    unsigned list = widgetView.Add(Gui::Kind::List, 0);
    widgetView.SetLayout(list, 0, 0, 1);
    widgetView.SetItems(list, examples, sizeof(examples) / sizeof(examples[0]));
    unsigned status = widgetView.Add(Gui::Kind::Label, 0, "Tab changes focus. Shift-arrows select. Ctrl-C/X/V edits.");
    unsigned close = widgetView.Add(Gui::Kind::Button, 0, "Close demo");
    (void)title;
    HackosWindowSpec spec = {200, 150, 720, 480, "GUI widgets"};
    if (!widgetWindow.Open(spec))
    {
        User::Error("Cannot open GUI widgets or load the font.\n");
        return 1;
    }
    bool closeDialog = false, pendingClose = false;
    const char* closeLabels[] = {"Close", "Cancel"};
    const char* aboutLabels[] = {"OK"};
    User::Print("GUI_WIDGETS_READY\n");
    while (widgetWindow.Handle())
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            Gui::Action action;
            uint64_t result = widgetWindow.Next(widgetView, action);
            if (result == HACKOS_ERROR)
            {
                widgetWindow.Close();
                break;
            }
            if (!result)
            {
                break;
            }
            if (action.kind == Gui::ActionKind::Close ||
                (action.kind == Gui::ActionKind::Activate && action.widget == close))
            {
                if (widgetView.DialogOpen())
                {
                    widgetWindow.ReplyClose(false);
                    User::Print("GUI_WIDGET_CLOSE_BLOCKED\n");
                    continue;
                }
                pendingClose = action.kind == Gui::ActionKind::Close;
                closeDialog = true;
                widgetView.OpenDialog("Close widgets?", "Close this demo, or cancel to keep editing.", closeLabels, 2,
                                      1);
                User::Print("GUI_WIDGET_DIALOG\n");
            }
            else if (action.kind == Gui::ActionKind::Activate && action.widget == about)
            {
                closeDialog = false;
                widgetView.OpenDialog("About the GUI library",
                                      "Reusable drawing, layout, widgets and dialogs.\nAll controls run in userspace.",
                                      aboutLabels, 1, 0);
                User::Print("GUI_WIDGET_ABOUT\n");
            }
            else if (action.kind == Gui::ActionKind::Dialog)
            {
                if (closeDialog && action.value == 0)
                {
                    if (pendingClose)
                    {
                        widgetWindow.ReplyClose(true);
                    }
                    else
                    {
                        widgetWindow.Close();
                    }
                }
                else
                {
                    if (pendingClose)
                    {
                        widgetWindow.ReplyClose(false);
                    }
                    User::Print("GUI_WIDGET_CANCEL\n");
                }
                pendingClose = closeDialog = false;
            }
            else if (action.kind == Gui::ActionKind::Activate && action.widget == apply)
            {
                widgetView.SetText(status, "Applied. The text and selection remain available.");
                User::Print("GUI_WIDGET_APPLY\n");
            }
            else if (action.kind == Gui::ActionKind::Change)
            {
                if (action.widget == field)
                {
                    widgetView.SetText(status, "Text edited. Ctrl-A selects all; Shift-arrows select a range.");
                }
                if (trace)
                {
                    printf("GUI_WIDGET_CHANGE %u %d\n", action.widget, action.value);
                }
            }
        }
        if (!widgetWindow.Handle())
        {
            break;
        }
        bool retry = false;
        if (widgetView.Dirty())
        {
            retry = !widgetWindow.Paint(widgetView);
            if (!retry && trace)
            {
                User::Print("GUI_WIDGET_PAINT\n");
            }
        }
        if (retry)
        {
            syscall_sleep(16);
        }
        else
        {
            widgetWindow.Wait();
        }
    }
    User::Print("GUI_WIDGETS_CLOSED\n");
    return 0;
}
