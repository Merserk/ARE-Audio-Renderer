#include "win/property_page.hpp"
#include "../../resources/resource.h"
#include <format>

namespace are::win {
namespace {
void status_text(HWND window, int id, const std::wstring& text) {
    wchar_t current[1024]{};
    GetDlgItemTextW(window, id, current, int(std::size(current)));
    if (text != current) SetDlgItemTextW(window, id, text.c_str());
}
std::wstring format_text(const AudioFormatStatus& format) {
    if (!format.valid_bits) return L"Inactive";
    if (format.type == AudioSampleType::float32) return L"32-bit float";
    if (format.type == AudioSampleType::float64) return L"64-bit float";
    if (format.valid_bits != format.container_bits)
        return std::format(L"{}-bit PCM ({}-bit container)", format.valid_bits, format.container_bits);
    return std::format(L"{}-bit PCM", format.valid_bits);
}
}
SettingsPage::~SettingsPage() { if (window_) DestroyWindow(window_); }
STDMETHODIMP SettingsPage::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_IPropertyPage) return E_NOINTERFACE;
    *out = static_cast<IPropertyPage*>(this); AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) SettingsPage::Release() { const auto n = --refs_; if (!n) delete this; return n; }
STDMETHODIMP SettingsPage::SetPageSite(IPropertyPageSite* site) { if (site && site_) return E_UNEXPECTED; site_ = site; return S_OK; }
STDMETHODIMP SettingsPage::Activate(HWND parent, LPCRECT rect, BOOL) {
    if (!parent || !rect) return E_POINTER;
    if (window_) return E_UNEXPECTED;
    window_ = CreateDialogParamW(module_handle, MAKEINTRESOURCEW(IDD_SETTINGS), parent, dialog_proc, reinterpret_cast<LPARAM>(this));
    if (!window_) return HRESULT_FROM_WIN32(GetLastError());
    Move(rect); return S_OK;
}
STDMETHODIMP SettingsPage::Deactivate() { if (!window_) return E_UNEXPECTED; DestroyWindow(window_); window_ = nullptr; return S_OK; }
STDMETHODIMP SettingsPage::GetPageInfo(PROPPAGEINFO* info) {
    if (!info) return E_POINTER;
    *info = {}; info->cb = sizeof(*info);
    info->pszTitle = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(renderer_name)));
    if (!info->pszTitle) return E_OUTOFMEMORY;
    std::memcpy(info->pszTitle, renderer_name, sizeof(renderer_name));
    // Page dimensions scale with the user's system DPI.
    const auto dpi = GetDpiForSystem();
    info->size = {MulDiv(600, int(dpi), 96), MulDiv(710, int(dpi), 96)};
    return S_OK;
}
STDMETHODIMP SettingsPage::SetObjects(ULONG count, IUnknown** objects) {
    if (!count) { settings_.Reset(); live_status_.Reset(); playback_.Reset(); processing_.Reset(); resampling_.Reset(); channels_.Reset(); seeking_.Reset(); return S_OK; }
    if (count != 1 || !objects || !objects[0]) return E_INVALIDARG;
    settings_.Reset(); live_status_.Reset(); playback_.Reset(); processing_.Reset(); resampling_.Reset(); channels_.Reset(); seeking_.Reset();
    const auto hr = objects[0]->QueryInterface(IID_PPV_ARGS(settings_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(live_status_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(playback_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(processing_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(resampling_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(channels_.GetAddressOf()));
    if (SUCCEEDED(hr)) objects[0]->QueryInterface(IID_PPV_ARGS(seeking_.GetAddressOf()));
    if (SUCCEEDED(hr) && window_) initialize();
    return hr;
}
STDMETHODIMP SettingsPage::Show(UINT command) { if (!window_) return E_UNEXPECTED; if (command != SW_SHOW && command != SW_SHOWNORMAL && command != SW_HIDE) return E_INVALIDARG; ShowWindow(window_, int(command)); return S_OK; }
STDMETHODIMP SettingsPage::Move(LPCRECT rect) { if (!window_ || !rect) return E_POINTER; MoveWindow(window_, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, TRUE); return S_OK; }
STDMETHODIMP SettingsPage::TranslateAccelerator(MSG* message) { if (!message) return E_POINTER; return window_ && IsDialogMessageW(window_, message) ? S_OK : S_FALSE; }
void SettingsPage::initialize() {
    loading_ = true;
    devices_ = enumerate_devices();
    Settings s;
    if (settings_) settings_->GetSettings(&s);
    const auto combo = GetDlgItem(window_, IDC_DEVICE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Select a device"));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices_[i].name.c_str()));
        if (devices_[i].clsid == s.driver) SendMessageW(combo, CB_SETCURSEL, i + 1, 0);
    }
    SetDlgItemInt(window_, IDC_CHANNEL, s.first_channel + 1, FALSE);
    const auto buffer = GetDlgItem(window_, IDC_BUFFER);
    SendMessageW(buffer, CB_RESETCONTENT, 0, 0);
    for (const UINT frames : {0u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u}) {
        const auto text = frames ? std::format(L"{} frames", frames) : std::wstring(L"Automatic");
        const auto index = SendMessageW(buffer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(buffer, CB_SETITEMDATA, WPARAM(index), frames);
        if (frames == s.buffer_frames) SendMessageW(buffer, CB_SETCURSEL, WPARAM(index), 0);
    }
    if (SendMessageW(buffer, CB_GETCURSEL, 0, 0) == CB_ERR) {
        const auto text = std::format(L"{} frames", s.buffer_frames);
        const auto index = SendMessageW(buffer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(buffer, CB_SETITEMDATA, WPARAM(index), s.buffer_frames); SendMessageW(buffer, CB_SETCURSEL, WPARAM(index), 0);
    }
    CheckDlgButton(window_, IDC_KEEP_RATE, s.keep_device_rate ? BST_CHECKED : BST_UNCHECKED);
    PlaybackOptions options;
    if (playback_) playback_->GetPlaybackOptions(&options);
    CheckDlgButton(window_, IDC_SMOOTH, options.smooth_transitions ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(window_, IDC_SMOOTH), playback_ != nullptr);
    ProcessingOptions precision;
    if (processing_) processing_->GetProcessingOptions(&precision);
    const auto pcm=GetDlgItem(window_,IDC_PCM_BITS);
    SendMessageW(pcm,CB_RESETCONTENT,0,0);
    for (const UINT bits : {0u,16u,24u,32u}) {
        const auto text=bits ? std::format(L"{}-bit PCM",bits) : std::wstring(L"Automatic (device format)");
        const auto index=SendMessageW(pcm,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(pcm,CB_SETITEMDATA,WPARAM(index),bits);
        if (bits==precision.output_bits) SendMessageW(pcm,CB_SETCURSEL,WPARAM(index),0);
    }
    EnableWindow(pcm,processing_ != nullptr);
    ResamplingOptions src;
    if (resampling_) resampling_->GetResamplingOptions(&src);
    const auto algorithm=GetDlgItem(window_,IDC_SRC_ALGORITHM);
    SendMessageW(algorithm,CB_RESETCONTENT,0,0);
    for (const auto choice : {SrcAlgorithm::r8brain,SrcAlgorithm::sinc}) {
        const auto text=choice==SrcAlgorithm::r8brain ? L"r8brain" : L"Sinc";
        const auto index=SendMessageW(algorithm,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(algorithm,CB_SETITEMDATA,WPARAM(index),LPARAM(choice));
        if (choice==src.algorithm) SendMessageW(algorithm,CB_SETCURSEL,WPARAM(index),0);
    }
    EnableWindow(algorithm,resampling_ != nullptr);
    dirty_ = false; loading_ = false; refresh_status();
}
void SettingsPage::dirty() {
    if (loading_) return;
    apply_message_.clear();
    dirty_ = true; if (site_) site_->OnStatusChange(PROPPAGESTATUS_DIRTY);
    refresh_status();
}
void SettingsPage::refresh_status() {
    if (!settings_) return;
    LiveStatus live;
    if (!live_status_ || FAILED(live_status_->GetLiveStatus(&live))) {
        status_text(window_, IDC_STATUS, L"Live status is unavailable. Restart MPC-HC after updating the renderer.");
        return;
    }
    const auto& status = live.engine;
    ProcessingStatus conversion{};
    if (processing_) processing_->GetProcessingStatus(&conversion);
    ResamplingStatus src{};
    if (resampling_) resampling_->GetResamplingStatus(&src);
    const auto src_name=src.algorithm==SrcAlgorithm::sinc ? L"Sinc" : L"r8brain";
    double speed=1.0;
    if (seeking_) seeking_->GetRate(&speed);
    std::wstring state;
    if (FAILED(status.error)) state = L"Error";
    else if (!live.in_graph) state = L"No playback";
    else if (!live.connected) state = L"No audio";
    else if (live.playback == State_Stopped) state = L"Stopped";
    else if (live.playback == State_Paused) state = L"Paused";
    else if (live.completed) state = L"Finished";
    else if (status.playing) state = L"Playing";
    else state = L"Starting";
    if (status.opened) state += std::format(L" \u00b7 {:g}\u00d7",speed);
    if (status.opened && live.settings_pending && status.driver_name[0]) state += std::format(L" \u00b7 {}",status.driver_name);
    status_text(window_, IDC_PLAYBACK, state);
    status_text(window_, IDC_INPUT_FORMAT, format_text(live.input));
    status_text(window_, IDC_OUTPUT_FORMAT, live.mixed_output ? L"Mixed formats" : conversion.pcm_bits && conversion.pcm_bits!=live.output.valid_bits
        ? std::format(L"{}-bit PCM (ASIO: {})",conversion.pcm_bits,format_text(live.output)) : format_text(live.output));
    ChannelStatus routing{};
    if (channels_) channels_->GetChannelStatus(&routing);
    auto channels=status.channels==1 ? std::wstring(L"Mono") : status.channels==2 ? std::wstring(L"Stereo") : std::format(L"{} channels",status.channels);
    if (routing.downmix || routing.mono_duplicate)
        channels += routing.output_channels == 1 ? L" \u2192 Mono" : L" \u2192 Stereo";
    const auto rate=conversion.input_rate!=conversion.output_rate
        ? std::format(L"{:g} \u2192 {:g} kHz",conversion.input_rate/1000.0,conversion.output_rate/1000.0)
        : std::format(L"{:g} kHz",status.sample_rate/1000.0);
    status_text(window_, IDC_SAMPLE_RATE, status.opened
        ? conversion.resampling ? std::format(L"{} \u00b7 {} \u00b7 {}",rate,channels,src_name) : std::format(L"{} \u00b7 {}",rate,channels)
        : L"Inactive");
    status_text(window_, IDC_BUFFER_STATUS, status.opened && status.sample_rate
        ? std::format(L"{} frames \u00b7 {:.1f} ms output", status.buffer_frames, 1000.0 * status.output_latency_frames / status.sample_rate)
        : L"Inactive");
    SignalStatus signal{};
    const bool signal_available=playback_ && SUCCEEDED(playback_->GetSignalStatus(&signal));
    status_text(window_,IDC_GAIN,status.opened && signal_available
        ? signal.volume_db100==-10000 ? std::wstring(L"Muted") : std::format(L"{:.1f} dB",signal.volume_db100/100.0) : L"Inactive");
    status_text(window_, IDC_ACTIVITY, status.opened
        ? std::format(L"{} frames \u00b7 {} dropouts \u00b7 {} driver overloads", status.delivered_frames, status.underruns, status.overloads)
        : L"Inactive");
    std::wstring text;
    if (FAILED(status.error)) text = std::format(L"Error 0x{:08X}: {}", UINT(status.error), status.detail);
    else if (!apply_message_.empty() && apply_message_ != L"Settings saved.") text = apply_message_;
    else if (live.settings_pending) text = L"Settings saved. Stop and restart playback to apply changes.";
    else if (!live.in_graph) text = L"Open settings during playback in MPC-HC to see output details.";
    else if (!status.opened) text = L"Start playback to see output details.";
    else if (!signal_available) text=L"Processing status unavailable.";
    else {
        std::wstring processing;
        const auto add=[&processing](const std::wstring& item) { if (!processing.empty()) processing+=L", "; processing+=item; };
        if (speed!=1.0) add(L"playback speed");
        if (conversion.resampling && (speed==1.0 || conversion.input_rate!=conversion.output_rate)) add(std::format(L"{} resampling",src_name));
        if (conversion.precision_reduced) add(L"output conversion");
        if (routing.downmix) add(L"channel downmix");
        else if (routing.mono_duplicate) add(L"mono to both outputs");
        if (signal.volume_db100==-10000) add(L"mute");
        else if (signal.volume_db100!=0) add(L"volume adjustment");
        if (signal.transitioning) add(L"transition smoothing");
        text=processing.empty() ? signal.smooth_transitions ? L"Decoded samples preserved between transitions." : L"Decoded samples preserved."
            : L"Processing: "+processing+L".";
        if (speed!=1.0) text+=L" Pitch follows speed.";
    }
    status_text(window_, IDC_STATUS, text);
    const auto selected = SendDlgItemMessageW(window_, IDC_DEVICE, CB_GETCURSEL, 0, 0);
    std::wstring note = L"Windows audio sharing depends on the ASIO driver.";
    if (selected > 0 && std::size_t(selected) <= devices_.size()) {
        const auto& name = devices_[std::size_t(selected) - 1].name;
        if (name.find(L"FL Studio") != std::wstring::npos || name.find(L"FlexASIO") != std::wstring::npos) note = L"Windows audio bridge. System mixing may modify the output.";
        else if (name.find(L"ASIO4ALL") != std::wstring::npos) note = L"ASIO4ALL may prevent other apps from using this audio device.";
    }
    status_text(window_, IDC_NOTE, note);
}
STDMETHODIMP SettingsPage::Apply() {
    if (!settings_ || !window_) return E_UNEXPECTED;
    try {
        const auto selected = SendDlgItemMessageW(window_, IDC_DEVICE, CB_GETCURSEL, 0, 0);
        if (selected <= 0 || std::size_t(selected) > devices_.size()) { apply_message_ = L"Select an installed ASIO device."; refresh_status(); return E_INVALIDARG; }
        Settings s;
        s.driver = devices_[std::size_t(selected) - 1].clsid;
        BOOL valid = FALSE;
        const auto channel = GetDlgItemInt(window_, IDC_CHANNEL, &valid, FALSE);
        if (!valid || channel < 1 || channel > 1024) { apply_message_ = L"Enter an output channel between 1 and 1024."; refresh_status(); return E_INVALIDARG; }
        s.first_channel = channel - 1;
        const auto index = SendDlgItemMessageW(window_, IDC_BUFFER, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR) return E_INVALIDARG;
        s.buffer_frames = UINT(SendDlgItemMessageW(window_, IDC_BUFFER, CB_GETITEMDATA, WPARAM(index), 0));
        s.keep_device_rate = IsDlgButtonChecked(window_, IDC_KEEP_RATE) == BST_CHECKED;
        const auto hr = settings_->SetSettings(&s);
        if (FAILED(hr)) { apply_message_ = L"Could not save settings."; refresh_status(); return hr; }
        if (playback_) {
            const PlaybackOptions options{IsDlgButtonChecked(window_, IDC_SMOOTH) == BST_CHECKED};
            if (FAILED(playback_->SetPlaybackOptions(&options))) { apply_message_ = L"Could not save transition settings."; refresh_status(); return E_FAIL; }
        }
        if (processing_) {
            const auto pcm_index=SendDlgItemMessageW(window_,IDC_PCM_BITS,CB_GETCURSEL,0,0);
            if (pcm_index==CB_ERR) return E_INVALIDARG;
            const ProcessingOptions precision{UINT(SendDlgItemMessageW(window_,IDC_PCM_BITS,CB_GETITEMDATA,WPARAM(pcm_index),0))};
            const auto result=processing_->SetProcessingOptions(&precision);
            if (FAILED(result)) { apply_message_=L"Could not save output bit depth."; refresh_status(); return result; }
        }
        if (resampling_) {
            const auto src_index=SendDlgItemMessageW(window_,IDC_SRC_ALGORITHM,CB_GETCURSEL,0,0);
            if (src_index==CB_ERR) return E_INVALIDARG;
            const ResamplingOptions src{SrcAlgorithm(SendDlgItemMessageW(window_,IDC_SRC_ALGORITHM,CB_GETITEMDATA,WPARAM(src_index),0))};
            const auto result=resampling_->SetResamplingOptions(&src);
            if (FAILED(result)) { apply_message_=L"Could not save sample rate converter."; refresh_status(); return result; }
        }
        apply_message_ = L"Settings saved.";
        dirty_ = false; if (site_) site_->OnStatusChange(PROPPAGESTATUS_CLEAN); refresh_status(); return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
INT_PTR CALLBACK SettingsPage::dialog_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* page = reinterpret_cast<SettingsPage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        page = reinterpret_cast<SettingsPage*>(lp); page->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, lp);
        try { page->initialize(); SetTimer(window, 1, 500, nullptr); } catch (...) { SetDlgItemTextW(window, IDC_STATUS, L"Could not enumerate ASIO devices."); }
        return TRUE;
    }
    if (!page) return FALSE;
    try {
        if (message == WM_TIMER) { page->refresh_status(); return TRUE; }
        if (message == WM_COMMAND) {
            const auto id = LOWORD(wp), notification = HIWORD(wp);
            if ((id == IDC_DEVICE || id == IDC_BUFFER || id == IDC_PCM_BITS || id == IDC_SRC_ALGORITHM) && notification == CBN_SELCHANGE) page->dirty();
            if (id == IDC_CHANNEL && notification == EN_CHANGE) page->dirty();
            if ((id == IDC_KEEP_RATE || id == IDC_SMOOTH) && notification == BN_CLICKED) page->dirty();
        }
        if (message == WM_DESTROY) KillTimer(window, 1);
    } catch (...) { SetDlgItemTextW(window, IDC_STATUS, L"Settings operation failed."); }
    return FALSE;
}
} // namespace are::win
