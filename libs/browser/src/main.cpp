#include "hackos/gui.h"
#include "hackos/stdlib.h"
#include "hackos/runtime/html.h"
#include "fetch.h"

using namespace User;
static Gui::Window window;
static Gui::View view;
static Hackos::Html::Document* document;
static Fetch* fetch;
static unsigned addressField, pageWidget, statusLabel, backButton, stopButton;
static unsigned top, selectedLink, historyCount;
static char current[256], pending[256], history[16][256];
static int historyTarget;
static bool hasPage;

static uint64_t Now()
{
    HackosClock clock = {};
    syscall_clock(&clock);
    return clock.ticks;
}

static void Status(const char* message)
{
    view.SetText(statusLabel, message);
}

static void Scroll(int amount, unsigned visible)
{
    int next = (int)top + amount;
    unsigned maximum = document->lineCount > visible ? document->lineCount - visible : 0;
    top = next < 0 ? 0 : (unsigned)next > maximum ? maximum : next;
    view.Invalidate();
}

static void Draw(void*, Gui::Canvas& canvas, const Gui::Font& font, Gui::Rect bounds, bool focus)
{
    Gui::Rect original = canvas.Clip();
    canvas.Fill(bounds, 0xfffaf9f5);
    canvas.Border(bounds, focus ? 0xff5173a5 : 0xffb8b8b0);
    unsigned cols = bounds.width > 32 ? (bounds.width - 32) / 8 : 1;
    document->Layout(cols);
    unsigned visible = bounds.height > 16 ? (bounds.height - 16) / 20 : 1;
    unsigned maximum = document->lineCount > visible ? document->lineCount - visible : 0;
    if (top > maximum)
    {
        top = maximum;
    }
    Gui::Rect clip = Gui::Intersect(canvas.Clip(), {bounds.x + 8, bounds.y + 8, bounds.width - 24, bounds.height - 16});
    canvas.SetClip(clip);
    int first = (clip.y - bounds.y - 8) / 20;
    if (first < 0)
    {
        first = 0;
    }
    unsigned start = top + first;
    unsigned end = top + (clip.y + clip.height - bounds.y - 8) / 20 + 1;
    if (end > document->lineCount)
    {
        end = document->lineCount;
    }
    if (start < end)
    {
        for (unsigned i = document->first[start]; i < document->first[end]; ++i)
        {
            const auto& g = document->glyphs[i];
            if (g.text == '\n')
            {
                continue;
            }
            int x = bounds.x + 8 + document->columns[i] * 8;
            int y = bounds.y + 8 + ((int)document->rows[i] - (int)top) * 20;
            uint32_t ink = g.link ? 0xff2459aa : g.style & Hackos::Html::Heading ? 0xff23485a : 0xff252b31;
            if (g.link && g.link == selectedLink)
            {
                canvas.Fill({x, y, 8, 18}, 0xffdce8ff);
            }
            char text[2] = {g.text, 0};
            canvas.Text(font, x, y, text, ink);
            if (g.style & Hackos::Html::Bold)
            {
                canvas.Text(font, x + 1, y, text, ink);
            }
            if (g.link)
            {
                canvas.Fill({x, y + 16, 8, 1}, ink);
            }
        }
    }
    canvas.SetClip(Gui::Intersect(original, {bounds.x + bounds.width - 12, bounds.y + 4, 8, bounds.height - 8}));
    int track = bounds.height - 8;
    if (track > 0)
    {
        canvas.Fill({bounds.x + bounds.width - 12, bounds.y + 4, 8, track}, 0xffe2e3de);
        int thumb = document->lineCount > visible ? track * visible / document->lineCount : track;
        if (thumb < 8)
        {
            thumb = 8;
        }
        if (thumb > track)
        {
            thumb = track;
        }
        int offset = maximum ? (track - thumb) * top / maximum : 0;
        canvas.Fill({bounds.x + bounds.width - 12, bounds.y + 4 + offset, 8, thumb}, 0xff7f919d);
    }
}

static Gui::Action Input(void*, const HackosWindowEvent2& input, Gui::Rect bounds)
{
    const auto& event = input.event;
    unsigned visible = bounds.height > 16 ? (bounds.height - 16) / 20 : 1;
    if (event.type == HACKOS_WINDOW_WHEEL)
    {
        Scroll((int32_t)event.key * 3, visible);
    }
    else if (event.type == HACKOS_WINDOW_MOUSE && (event.buttons & 1))
    {
        if (event.x >= bounds.x + bounds.width - 16)
        {
            Scroll(event.y < bounds.y + bounds.height / 2 ? -(int)visible : (int)visible, visible);
        }
        else if (event.x >= bounds.x + 8 && event.y >= bounds.y + 8)
        {
            unsigned row = top + (event.y - bounds.y - 8) / 20;
            unsigned col = (event.x - bounds.x - 8) / 8;
            if (row < document->lineCount)
            {
                for (unsigned i = document->first[row]; i < document->first[row + 1]; ++i)
                {
                    if (document->columns[i] == col && document->glyphs[i].link)
                    {
                        return {Gui::ActionKind::Activate, 0, document->glyphs[i].link};
                    }
                }
            }
        }
    }
    else if (event.type == HACKOS_WINDOW_KEY_DOWN)
    {
        if (event.key == 0xc8)
        {
            Scroll(-1, visible);
        }
        else if (event.key == 0xd0)
        {
            Scroll(1, visible);
        }
        else if (event.key == 0xc9)
        {
            Scroll(-(int)visible, visible);
        }
        else if (event.key == 0xd1 || event.key == 0x39)
        {
            Scroll(visible, visible);
        }
        else if (event.key == 0xc7)
        {
            Scroll(-(int)document->lineCount, visible);
        }
        else if (event.key == 0xcf)
        {
            Scroll(document->lineCount, visible);
        }
        else if ((event.key == 0xcd || event.key == 0xcb) && document->linkCount)
        {
            selectedLink = event.key == 0xcd ? selectedLink % document->linkCount + 1
                                          : selectedLink > 1 ? selectedLink - 1 : document->linkCount;
            for (unsigned i = 0; i < document->count; ++i)
            {
                if (document->glyphs[i].link == selectedLink)
                {
                    top = document->rows[i];
                    break;
                }
            }
            Scroll(0, visible);
        }
        else if (event.key == HACKOS_PHYSICAL_ENTER && selectedLink)
        {
            return {Gui::ActionKind::Activate, 0, (int)selectedLink};
        }
    }
    return {Gui::ActionKind::None, Gui::NoWidget, 0};
}

static void Navigate(const char* url, int target = -1)
{
    char resolved[256];
    if (!Hackos::Html::Resolve(*current ? current : "http://10.0.2.2:8080/browser/", url, resolved))
    {
        Status("Invalid/unsupported URL. Use HTTP, at most 255 bytes; HTTPS is not available.");
        return;
    }
    Copy(pending, resolved);
    historyTarget = target;
    view.SetText(addressField, pending);
    Status("Loading... Stop cancels the request. The previous page stays visible.");
    view.SetEnabled(stopButton, true);
    fetch->Start(pending, Now());
}

static void Finish()
{
    fetch->done = false;
    view.SetEnabled(stopButton, false);
    char message[256];
    if (!fetch->good)
    {
        snprintf(message, sizeof(message), "HTTP %u: %s", fetch->status, fetch->error ? fetch->error : "Load failed.");
        Status(message);
        Print("BROWSER_ERROR\n");
        return;
    }
    if (historyTarget >= 0)
    {
        historyCount = historyTarget;
    }
    else if (hasPage && !Equal(current, pending))
    {
        if (historyCount == 16)
        {
            for (unsigned i = 1; i < 16; ++i)
            {
                Copy(history[i - 1], history[i]);
            }
            --historyCount;
        }
        Copy(history[historyCount++], current);
    }
    Copy(current, pending);
    hasPage = true;
    document->Load(fetch->body, fetch->size);
    top = selectedLink = 0;
    view.SetEnabled(backButton, historyCount != 0);
    view.SetFocus(pageWidget);
    snprintf(message, sizeof(message), "HTTP %u | %u bytes | %u links%s", fetch->status, fetch->size,
             document->linkCount, document->limited ? " | Display limit reached" : " | HTML text only");
    Status(message);
    Print("BROWSER_LOADED\n");
}

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && (Equal(argv[1], "--help") || Equal(argv[1], "-h"))))
    {
        Print("Usage: browser [http://host[:port]/path]\n"
              "Minimal HTML window with links, Back/Reload and scrolling.\n"
              "Default: http://10.0.2.2:8080/browser/ | 32 KiB pages, 255-byte URLs.\n");
        return argc > 2 ? 2 : 0;
    }
    document = (Hackos::Html::Document*)calloc(1, sizeof(Hackos::Html::Document));
    if (!document) return 1;
    fetch = new Fetch();
    if (!fetch)
    {
        return 1;
    }
    view.Init();
    auto toolbar = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(toolbar, 0, 32, 0, 0, 6);
    backButton = view.Add(Gui::Kind::Button, toolbar, "Back");
    auto reload = view.Add(Gui::Kind::Button, toolbar, "Reload");
    auto home = view.Add(Gui::Kind::Button, toolbar, "Home");
    stopButton = view.Add(Gui::Kind::Button, toolbar, "Stop");
    addressField = view.Add(Gui::Kind::TextField, toolbar, "http://10.0.2.2:8080/browser/");
    view.SetLayout(addressField, 0, 32, 1);
    auto go = view.Add(Gui::Kind::Button, toolbar, "Go");
    pageWidget = view.Add(Gui::Kind::Custom, 0);
    view.SetLayout(pageWidget, 0, 0, 1, 0, 0);
    view.SetCustom(pageWidget, {nullptr, Draw, Input});
    view.Add(Gui::Kind::Label, 0, "Scroll: wheel / PgUp / PgDn | Links: click, or Left/Right then Enter | Tab: toolbar");
    statusLabel = view.Add(Gui::Kind::Label, 0, "Ready");
    view.SetEnabled(backButton, false);
    view.SetEnabled(stopButton, false);
    HackosWindowSpec spec = {100, 60, 1080, 700, "HackOS Browser"};
    if (!window.Open(spec))
    {
        delete fetch;
        free(document);
        return 1;
    }
    Navigate(argc > 1 ? argv[1] : "http://10.0.2.2:8080/browser/");
    Print("BROWSER_READY\n");
    while (window.Handle())
    {
        Gui::Action action;
        for (unsigned n = 0; n < 32; ++n)
        {
            auto result = window.Next(view, action);
            if (result == HACKOS_ERROR)
            {
                window.Close();
                break;
            }
            if (result != 1)
            {
                break;
            }
            if (action.kind == Gui::ActionKind::Close)
            {
                window.ReplyClose(true);
                break;
            }
            if (action.kind != Gui::ActionKind::Activate)
            {
                continue;
            }
            if (action.widget == go || action.widget == addressField)
            {
                Navigate(view.Get(addressField)->text);
            }
            else if (action.widget == home)
            {
                Navigate("http://10.0.2.2:8080/browser/");
            }
            else if (action.widget == reload)
            {
                Navigate(*current ? current : view.Get(addressField)->text);
            }
            else if (action.widget == backButton && historyCount)
            {
                Navigate(history[historyCount - 1], historyCount - 1);
            }
            else if (action.widget == stopButton)
            {
                fetch->Cancel();
                fetch->done = false;
                view.SetEnabled(stopButton, false);
                Status("Loading stopped. Previous page kept.");
            }
            else if (action.widget == pageWidget && action.value > 0 && (unsigned)action.value <= document->linkCount)
            {
                Navigate(document->links[action.value - 1]);
            }
        }
        if (!window.Handle())
        {
            break;
        }
        fetch->Step(Now());
        if (fetch->done)
        {
            Finish();
        }
        if (view.Dirty() && !window.Paint(view))
        {
            syscall_sleep(16);
            continue;
        }
        HackosPollFd poll[2] = {{window.Handle(), HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0},
                               {fetch->socket, HACKOS_POLL_TCP | fetch->Events(), 0}};
        syscall_poll(poll, fetch->socket ? 2 : 1, fetch->Busy() ? 20 : HACKOS_WAIT_FOREVER);
    }
    delete fetch;
    free(document);
    return 0;
}
