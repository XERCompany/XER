// xer3 — TypeScript definitions for the native addon (xer.node).
//
// Generated from runtime introspection of the built module: every class,
// namespace and method name below exists in xer.node. Parameter and most
// return types are intentionally `any` — refine them as the API stabilizes.
// Boolean-returning predicates (Is*/Has*/Can*) are typed.

declare namespace xer3 {
  namespace UI {
    class Accordion {
      constructor(...args: any[]);
      SetItems(...args: any[]): any;
      SetItemOpen(...args: any[]): any;
      IsItemOpen(): boolean;
      SetOnToggled(...args: any[]): any;
    }
    class Alert {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      SetDescription(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class AlertDialog {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetPanelWidth(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      Close(...args: any[]): any;
      WasConfirmed(...args: any[]): any;
    }
    class Application {
      constructor(...args: any[]);
      Initialize(...args: any[]): any;
      Run(...args: any[]): any;
      Shutdown(...args: any[]): any;
      IsInitialized(): boolean;
      IsRunning(): boolean;
      SetAppName(...args: any[]): any;
      GetAppName(...args: any[]): any;
    }
    class AppTitleBar {
      constructor(...args: any[]);
      SetWorkspaceLabel(...args: any[]): any;
      GetWorkspaceLabel(...args: any[]): any;
      SetWorkspaceItems(...args: any[]): any;
      SetNavActions(...args: any[]): any;
      SetMenus(...args: any[]): any;
      ClearMenus(...args: any[]): any;
      SetCenterCommand(...args: any[]): any;
      SetCenterCommandValue(...args: any[]): any;
      GetCenterCommandValue(...args: any[]): any;
      SetLeftPanelToggle(...args: any[]): any;
      SetRightPanelToggle(...args: any[]): any;
      SetThemeToggle(...args: any[]): any;
      SetAvatar(...args: any[]): any;
      SetActionPill(...args: any[]): any;
      ClearActionPill(...args: any[]): any;
      SetOnWindowCommand(...args: any[]): any;
      SetMaximized(...args: any[]): any;
      GetMaximized(...args: any[]): any;
      IsInteractiveAt(): boolean;
      HasInteractiveAt(): boolean;
    }
    class AspectRatio {
      constructor(...args: any[]);
      SetRatio(...args: any[]): any;
      GetRatio(...args: any[]): any;
    }
    class Attachment {
      constructor(...args: any[]);
      SetFilename(...args: any[]): any;
      GetFilename(...args: any[]): any;
      SetMeta(...args: any[]): any;
      GetMeta(...args: any[]): any;
      SetOnRemove(...args: any[]): any;
    }
    class Avatar {
      constructor(...args: any[]);
      SetName(...args: any[]): any;
      GetName(...args: any[]): any;
      SetSize(...args: any[]): any;
    }
    class Badge {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class Breadcrumb {
      constructor(...args: any[]);
      SetItems(...args: any[]): any;
      SetOnNavigate(...args: any[]): any;
    }
    class Bubble {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetAlign(...args: any[]): any;
      GetAlign(...args: any[]): any;
    }
    class Button {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetStyle(...args: any[]): any;
      SetLoading(...args: any[]): any;
      IsLoading(): boolean;
      SetOnClick(...args: any[]): any;
    }
    class ButtonGroup {
      constructor(...args: any[]);
      SetOrientation(...args: any[]): any;
      GetOrientation(...args: any[]): any;
    }
    class Calendar {
      constructor(...args: any[]);
      SetDate(...args: any[]): any;
      GetDate(...args: any[]): any;
      GoToMonth(...args: any[]): any;
      SetMinDate(...args: any[]): any;
      SetMaxDate(...args: any[]): any;
      SetOnSelect(...args: any[]): any;
    }
    class Carousel {
      constructor(...args: any[]);
      AddSlide(...args: any[]): any;
      ClearSlides(...args: any[]): any;
      SetIndex(...args: any[]): any;
      GetIndex(...args: any[]): any;
      GetCount(...args: any[]): any;
      SetLoop(...args: any[]): any;
      SetShowArrows(...args: any[]): any;
      SetShowDots(...args: any[]): any;
      Next(...args: any[]): any;
      Prev(...args: any[]): any;
      SetOnIndexChanged(...args: any[]): any;
    }
    class Chart {
      constructor(...args: any[]);
      SetType(...args: any[]): any;
      SetSeries(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetShowGrid(...args: any[]): any;
      SetShowLegend(...args: any[]): any;
      SetOnPointClick(...args: any[]): any;
    }
    class Checkbox {
      constructor(...args: any[]);
      IsChecked(): boolean;
      SetChecked(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class Collapsible {
      constructor(...args: any[]);
      SetOpen(...args: any[]): any;
      IsOpen(): boolean;
      GetProgress(...args: any[]): any;
      SetOnToggled(...args: any[]): any;
    }
    class Combobox {
      constructor(...args: any[]);
      SetOptions(...args: any[]): any;
      SetValue(...args: any[]): any;
      GetValue(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetOnChange(...args: any[]): any;
    }
    class Command {
      constructor(...args: any[]);
      SetItems(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      Open(...args: any[]): any;
      Close(...args: any[]): any;
      IsOpen(): boolean;
      SetOnOpenChange(...args: any[]): any;
    }
    class ContextMenu {
      constructor(...args: any[]);
      SetAnchor(...args: any[]): any;
      Close(...args: any[]): any;
    }
    class Dialog {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetPanelWidth(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      Close(...args: any[]): any;
      WasConfirmed(...args: any[]): any;
    }
    class Direction {
      constructor(...args: any[]);
      SetDirection(...args: any[]): any;
      GetDirection(...args: any[]): any;
    }
    class Drawer {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetSnapPoint(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      Close(...args: any[]): any;
    }
    class DropdownMenu {
      constructor(...args: any[]);
      SetTrigger(...args: any[]): any;
      SetItems(...args: any[]): any;
      Open(...args: any[]): any;
      Close(...args: any[]): any;
      IsOpen(): boolean;
      SetOnOpenChange(...args: any[]): any;
    }
    class Empty {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetDescription(...args: any[]): any;
      GetDescription(...args: any[]): any;
    }
    class Field {
      constructor(...args: any[]);
      SetLabel(...args: any[]): any;
      GetLabel(...args: any[]): any;
      SetDescription(...args: any[]): any;
      GetDescription(...args: any[]): any;
      SetError(...args: any[]): any;
      GetError(...args: any[]): any;
    }
    class Form {
      constructor(...args: any[]);
      SetColumns(...args: any[]): any;
      GetColumns(...args: any[]): any;
      SetGap(...args: any[]): any;
      GetGap(...args: any[]): any;
    }
    class HoverCard {
      constructor(...args: any[]);
      SetTrigger(...args: any[]): any;
      SetTitle(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetDescription(...args: any[]): any;
      GetDescription(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOpenDelay(...args: any[]): any;
      SetActionLabel(...args: any[]): any;
      SetOnAction(...args: any[]): any;
    }
    class Input {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      SetValue(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetPasswordMode(...args: any[]): any;
      Undo(...args: any[]): any;
      Redo(...args: any[]): any;
      SetOnValueChanged(...args: any[]): any;
      SetOnSubmit(...args: any[]): any;
    }
    class InputGroup {
      constructor(...args: any[]);
      SetPrefix(...args: any[]): any;
      GetPrefix(...args: any[]): any;
      SetSuffix(...args: any[]): any;
      GetSuffix(...args: any[]): any;
    }
    class InputOTP {
      constructor(...args: any[]);
      SetLength(...args: any[]): any;
      GetLength(...args: any[]): any;
      SetCode(...args: any[]): any;
      GetCode(...args: any[]): any;
      SetOnComplete(...args: any[]): any;
    }
    class Item {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetDescription(...args: any[]): any;
      GetDescription(...args: any[]): any;
      SetSelected(...args: any[]): any;
      IsSelected(): boolean;
    }
    class Kbd {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
    }
    class Label {
      constructor(...args: any[]);
      SetPosition(...args: any[]): any;
      SetSize(...args: any[]): any;
      SetBounds(...args: any[]): any;
      GetBounds(...args: any[]): any;
      SetVisible(...args: any[]): any;
      IsVisible(): boolean;
      SetEnabled(...args: any[]): any;
      SetTooltip(...args: any[]): any;
      GetTooltip(...args: any[]): any;
      ShowOverlay(...args: any[]): any;
      HideOverlay(...args: any[]): any;
      Invalidate(...args: any[]): any;
      IsEnabled(): boolean;
      SetFocusable(...args: any[]): any;
      IsFocusable(): boolean;
      SetFocused(...args: any[]): any;
      IsFocused(): boolean;
      SetHovered(...args: any[]): any;
      IsHovered(): boolean;
      IsEffectivelyVisible(): boolean;
      SetClipsChildren(...args: any[]): any;
      ClipsChildren(): boolean;
      SetVisibleRect(...args: any[]): any;
      GetVisibleRect(...args: any[]): any;
      SetParent(...args: any[]): any;
      GetParent(...args: any[]): any;
      GetChildren(...args: any[]): any;
      Tick(...args: any[]): any;
      TickTree(...args: any[]): any;
      GetContentHeight(...args: any[]): any;
      GetMinSize(...args: any[]): any;
      GetPreferredSize(...args: any[]): any;
      GetMaxSize(...args: any[]): any;
      AddChild(...args: any[]): any;
      RemoveChild(...args: any[]): any;
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
    }
    class Marker {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
    }
    class Message {
      constructor(...args: any[]);
      SetSender(...args: any[]): any;
      GetSender(...args: any[]): any;
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetTime(...args: any[]): any;
      GetTime(...args: any[]): any;
      SetAlign(...args: any[]): any;
      GetAlign(...args: any[]): any;
    }
    class MessageScroller {
      constructor(...args: any[]);
      AddMessage(...args: any[]): any;
      Clear(...args: any[]): any;
      ScrollToBottom(...args: any[]): any;
      GetScrollY(...args: any[]): any;
    }
    class NativeSelect {
      constructor(...args: any[]);
      SetOptions(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
      GetSelectedIndex(...args: any[]): any;
      GetSelectedText(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      IsDropdownOpen(): boolean;
      SetOnChanged(...args: any[]): any;
    }
    class Pagination {
      constructor(...args: any[]);
      SetPageCount(...args: any[]): any;
      SetCurrentPage(...args: any[]): any;
      GetCurrentPage(...args: any[]): any;
      VisiblePages(...args: any[]): any;
      SetOnPageChanged(...args: any[]): any;
    }
    class Popover {
      constructor(...args: any[]);
      SetTrigger(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetTitle(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetSide(...args: any[]): any;
      SetAlign(...args: any[]): any;
      SetOpen(...args: any[]): any;
      IsOpen(): boolean;
      Open(...args: any[]): any;
      Close(...args: any[]): any;
      SetOnOpenChange(...args: any[]): any;
    }
    class Progress {
      constructor(...args: any[]);
      SetValue(...args: any[]): any;
      GetValue(...args: any[]): any;
    }
    class RadioGroup {
      constructor(...args: any[]);
      SetOptions(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
      GetSelectedIndex(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class Resizable {
      constructor(...args: any[]);
      AddPanel(...args: any[]): any;
      ClearPanels(...args: any[]): any;
      SetDirection(...args: any[]): any;
      SetHandleSize(...args: any[]): any;
      SetCollapsed(...args: any[]): any;
      SetOnLayout(...args: any[]): any;
    }
    class ScreenLoadingWidget {
      constructor(...args: any[]);
      Start(...args: any[]): any;
      IsFinished(): boolean;
      Tick(...args: any[]): any;
    }
    class ScrollArea {
      constructor(...args: any[]);
      SetContent(...args: any[]): any;
      GetContent(...args: any[]): any;
      ScrollTo(...args: any[]): any;
      GetScrollY(...args: any[]): any;
      GetScrollRange(...args: any[]): any;
    }
    class Select {
      constructor(...args: any[]);
      SetOptions(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
      GetSelectedIndex(...args: any[]): any;
      GetSelectedText(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      IsDropdownOpen(): boolean;
      SetOnChanged(...args: any[]): any;
    }
    class Sheet {
      constructor(...args: any[]);
      SetTitle(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetSide(...args: any[]): any;
      SetPanelSize(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      Close(...args: any[]): any;
    }
    class Sidebar {
      constructor(...args: any[]);
      SetHeader(...args: any[]): any;
      SetItems(...args: any[]): any;
      SetActiveIndex(...args: any[]): any;
      GetActiveIndex(...args: any[]): any;
      SetCollapsed(...args: any[]): any;
      SetOnSelect(...args: any[]): any;
    }
    class Skeleton {
      constructor(...args: any[]);
      SetSize(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class Slider {
      constructor(...args: any[]);
      SetRange(...args: any[]): any;
      SetValue(...args: any[]): any;
      GetValue(...args: any[]): any;
      IsDragging(): boolean;
      SetOnValueChanged(...args: any[]): any;
    }
    class Spinner {
      constructor(...args: any[]);
      SetPhase(...args: any[]): any;
    }
    class Switch {
      constructor(...args: any[]);
      IsChecked(): boolean;
      SetChecked(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class Table {
      constructor(...args: any[]);
      SetColumns(...args: any[]): any;
      SetRows(...args: any[]): any;
      SetHeader(...args: any[]): any;
      SetRowHeight(...args: any[]): any;
      SetSelectedRow(...args: any[]): any;
      GetSelectedRow(...args: any[]): any;
      ScrollTo(...args: any[]): any;
      SetOnRowClick(...args: any[]): any;
      SetOnCellClick(...args: any[]): any;
    }
    class Tabs {
      constructor(...args: any[]);
      AddTab(...args: any[]): any;
      ClearTabs(...args: any[]): any;
      GetActiveIndex(...args: any[]): any;
      SetActiveIndex(...args: any[]): any;
      SetStyle(...args: any[]): any;
      GetStyle(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class Textarea {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetReadOnly(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class TextInput {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      SetValue(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetPasswordMode(...args: any[]): any;
      Undo(...args: any[]): any;
      Redo(...args: any[]): any;
      SetOnValueChanged(...args: any[]): any;
      SetOnSubmit(...args: any[]): any;
    }
    class Toggle {
      constructor(...args: any[]);
      SetText(...args: any[]): any;
      GetText(...args: any[]): any;
      SetPressed(...args: any[]): any;
      IsPressed(): boolean;
      SetOnPressedChanged(...args: any[]): any;
    }
    class ToggleGroup {
      constructor(...args: any[]);
      SetOptions(...args: any[]): any;
      SetSelectedIndices(...args: any[]): any;
      GetSelectedIndices(...args: any[]): any;
      IsSelected(): boolean;
      SetOnChanged(...args: any[]): any;
    }
    class Tooltip {
      constructor(...args: any[]);
      SetAnchor(...args: any[]): any;
    }
    class WaveformWidget {
      constructor(...args: any[]);
      SetProgress(...args: any[]): any;
      SetPlaying(...args: any[]): any;
    }
    class Widget {
      constructor(...args: any[]);
      SetPosition(...args: any[]): any;
      SetSize(...args: any[]): any;
      SetBounds(...args: any[]): any;
      GetBounds(...args: any[]): any;
      SetVisible(...args: any[]): any;
      IsVisible(): boolean;
      SetEnabled(...args: any[]): any;
      SetTooltip(...args: any[]): any;
      GetTooltip(): string;
      ShowOverlay(...args: any[]): any;
      HideOverlay(...args: any[]): any;
      Invalidate(...args: any[]): any;
      IsEnabled(): boolean;
      SetFocusable(...args: any[]): any;
      IsFocusable(): boolean;
      SetFocused(...args: any[]): any;
      IsFocused(): boolean;
      SetHovered(...args: any[]): any;
      IsHovered(): boolean;
      IsEffectivelyVisible(): boolean;
      SetClipsChildren(...args: any[]): any;
      ClipsChildren(): boolean;
      SetVisibleRect(...args: any[]): any;
      GetVisibleRect(...args: any[]): any;
      SetParent(...args: any[]): any;
      GetParent(...args: any[]): any;
      GetChildren(...args: any[]): any;
      Tick(...args: any[]): any;
      TickTree(...args: any[]): any;
      GetContentHeight(): number;
      GetMinSize(...args: any[]): any;
      GetPreferredSize(...args: any[]): any;
      GetMaxSize(...args: any[]): any;
      AddChild(...args: any[]): any;
      RemoveChild(...args: any[]): any;
    }
    class Window {
      constructor(...args: any[]);
      Create(...args: any[]): any;
      Show(...args: any[]): any;
      Hide(...args: any[]): any;
      Close(...args: any[]): any;
      IsClosed(): boolean;
      IsVisible(): boolean;
      SetSize(...args: any[]): any;
      GetSize(...args: any[]): any;
      SetPosition(...args: any[]): any;
      GetPosition(...args: any[]): any;
      SetSizeLimits(...args: any[]): any;
      Minimize(...args: any[]): any;
      ToggleMaximize(...args: any[]): any;
      IsWindowMaximized(): boolean;
      SetContent(...args: any[]): any;
      GetContent(...args: any[]): any;
      SetTitle(...args: any[]): any;
      GetTitle(): string;
      SetTheme(...args: any[]): any;
      GetTheme(): string;
      SetCustomTitleBar(...args: any[]): any;
      HasCustomTitleBar(): boolean;
      SetCloseHides(...args: any[]): any;
      Invalidate(...args: any[]): any;
      GetLastRenderError(): string;
      GetRenderStats(): RenderStats;
      GetHWND(): number;
      SetOnClose(...args: any[]): any;
      SetOnResize(...args: any[]): any;
      SetFrameCallback(...args: any[]): any;
      SetTitleBarHitTest(...args: any[]): any;
    }
    function Separator(...args: any[]): any;
  }
  namespace audio {
    namespace analysis {
      class BeatTracker {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        Reset(...args: any[]): any;
        GetBeatEvents(...args: any[]): any;
        GetEstimatedBPM(...args: any[]): any;
        GetAverageStrength(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class FFTAnalyzer {
        constructor(...args: any[]);
        ComputeFFT(...args: any[]): any;
        GetFFTOutput(...args: any[]): any;
        GetMagnitudeSpectrum(...args: any[]): any;
        GetPhaseSpectrum(...args: any[]): any;
        AnalyzeSpectrum(...args: any[]): any;
        GetDominantFrequency(...args: any[]): any;
        GetFrequencyBands(...args: any[]): any;
        GetTotalPower(...args: any[]): any;
        GetRMSEnergy(...args: any[]): any;
        GetAnalysisReport(...args: any[]): any;
      }
      class HarmonicContent {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        GetFundamentalFrequency(...args: any[]): any;
        GetHarmonicRatio(...args: any[]): any;
        GetInharmonicity(...args: any[]): any;
        GetHarmonicSeries(...args: any[]): any;
        GetSummary(...args: any[]): any;
      }
      class LoudnessMeter {
        constructor(...args: any[]);
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        GetMomentaryLUFS(...args: any[]): any;
        GetShortTermLUFS(...args: any[]): any;
        GetIntegratedLUFS(...args: any[]): any;
        GetTruePeakDBFS(...args: any[]): any;
        GetProcessedBlocks(...args: any[]): any;
        GetHistory(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class OnsetDetector {
        constructor(...args: any[]);
        DetectOnsets(...args: any[]): any;
        Reset(...args: any[]): any;
        GetOnsetFrames(...args: any[]): any;
        GetOnsetTimesSeconds(...args: any[]): any;
        GetFluxCurve(...args: any[]): any;
        GetAverageStrength(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class PitchDetector {
        constructor(...args: any[]);
        DetectPitch(...args: any[]): any;
        IsPitched(): boolean;
        GetFundamentalFrequency(...args: any[]): any;
        GetHarmonics(...args: any[]): any;
        FrequencyToNote(...args: any[]): any;
        GetConfidence(...args: any[]): any;
        GetSampleRate(...args: any[]): any;
      }
      class PitchEstimator {
        constructor(...args: any[]);
        Estimate(...args: any[]): any;
        Reset(...args: any[]): any;
        GetLastPitch(...args: any[]): any;
        GetLastHarmonics(...args: any[]): any;
        GetLastNote(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class RmsPeak {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        Reset(...args: any[]): any;
        GetRMS(...args: any[]): any;
        GetPeak(...args: any[]): any;
        GetCrestFactor(...args: any[]): any;
        GetPeakDBFS(...args: any[]): any;
        GetWindowHistory(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Spectrogram {
        constructor(...args: any[]);
        Generate(...args: any[]): any;
        ExportAsRaw(...args: any[]): any;
        Reset(...args: any[]): any;
        GetMagnitudeMatrix(...args: any[]): any;
        GetFrameCount(...args: any[]): any;
        GetBinCount(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
    }
    namespace codec {
      class Aac {
        constructor(...args: any[]);
        Encode(...args: any[]): any;
        Decode(...args: any[]): any;
      }
      class FileStreamer {
        constructor(...args: any[]);
        OpenRead(...args: any[]): any;
        OpenWrite(...args: any[]): any;
        ReadBytes(...args: any[]): any;
        WriteBytes(...args: any[]): any;
        Close(...args: any[]): any;
      }
      class Flac {
        constructor(...args: any[]);
        Decode(...args: any[]): any;
      }
      class Id3 {
        constructor(...args: any[]);
        Parse(...args: any[]): any;
        Build(...args: any[]): any;
      }
      class Mp3 {
        constructor(...args: any[]);
        Encode(...args: any[]): any;
        EncodeInterleaved(...args: any[]): any;
        Decode(...args: any[]): any;
      }
      class Ogg {
        constructor(...args: any[]);
        Encode(...args: any[]): any;
        Decode(...args: any[]): any;
      }
      class WavFloat {
        constructor(...args: any[]);
        Encode32(...args: any[]): any;
        Decode32(...args: any[]): any;
      }
      class WavPcm {
        constructor(...args: any[]);
        Encode16(...args: any[]): any;
        Decode16(...args: any[]): any;
      }
    }
    namespace core {
      class AsyncManager {
        constructor(...args: any[]);
        Start(...args: any[]): any;
        Stop(...args: any[]): any;
        IsRunning(): boolean;
        GetPendingOperationCount(...args: any[]): any;
      }
      class BitDepth {
        constructor(...args: any[]);
        GetBytesPerSample(...args: any[]): any;
        IsFloatingPoint(): boolean;
        IsSupported(): boolean;
        NormalizeSamples(...args: any[]): any;
      }
      class BufferManager {
        constructor(...args: any[]);
        AcquireBuffer(...args: any[]): any;
        ReleaseBuffer(...args: any[]): any;
        Resize(...args: any[]): any;
        Clear(...args: any[]): any;
        GetAvailableBuffers(...args: any[]): any;
        GetTotalBuffers(...args: any[]): any;
        GetPoolStatus(...args: any[]): any;
      }
      class CacheManager {
        constructor(...args: any[]);
        CacheAudioMetadata(...args: any[]): any;
        GetCachedMetadata(...args: any[]): any;
        InvalidateMetadata(...args: any[]): any;
        ClearMetadataCache(...args: any[]): any;
        CacheEffectPreset(...args: any[]): any;
        GetCachedPreset(...args: any[]): any;
        InvalidatePreset(...args: any[]): any;
        ClearPresetCache(...args: any[]): any;
        CacheAnalysisResult(...args: any[]): any;
        GetCachedAnalysisResult(...args: any[]): any;
        InvalidateAnalysisResult(...args: any[]): any;
        ClearAnalysisCache(...args: any[]): any;
        GetMetadataCount(...args: any[]): any;
        GetPresetCount(...args: any[]): any;
        GetAnalysisCount(...args: any[]): any;
        SetDefaultMetadataTtl(...args: any[]): any;
        SetDefaultPresetTtl(...args: any[]): any;
        SetDefaultAnalysisTtl(...args: any[]): any;
        GetDefaultMetadataTtl(...args: any[]): any;
        GetDefaultPresetTtl(...args: any[]): any;
        GetDefaultAnalysisTtl(...args: any[]): any;
      }
      class ClockSync {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        IsInitialized(): boolean;
        UpdateClock(...args: any[]): any;
        SyncToExternal(...args: any[]): any;
        GetClockInfo(...args: any[]): any;
        GetCurrentFrame(...args: any[]): any;
        GetCurrentTime(...args: any[]): any;
        GetDriftPercentage(...args: any[]): any;
        CheckClockLock(...args: any[]): any;
        CalculateSampleRateRatio(...args: any[]): any;
      }
      class Engine {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        Start(...args: any[]): any;
        Stop(...args: any[]): any;
        IsRunning(): boolean;
        IsInitialized(): boolean;
        SetBufferSize(...args: any[]): any;
        SetSampleRate(...args: any[]): any;
        GetSampleRate(...args: any[]): any;
        GetBufferSize(...args: any[]): any;
        GetChannelCount(...args: any[]): any;
      }
      class EngineHost {
        constructor(...args: any[]);
        CreateEngine(...args: any[]): any;
        DestroyEngine(...args: any[]): any;
        StartAllEngines(...args: any[]): any;
        StopAllEngines(...args: any[]): any;
        GetEngineList(...args: any[]): any;
        SetGlobalSampleRate(...args: any[]): any;
        SetGlobalBufferSize(...args: any[]): any;
        GetEngineCount(...args: any[]): any;
        GetHostInfo(...args: any[]): any;
      }
      class Fifo {
        constructor(...args: any[]);
        Write(...args: any[]): any;
        Read(...args: any[]): any;
        GetAvailableFrames(...args: any[]): any;
        GetCapacity(...args: any[]): any;
        IsEmpty(): boolean;
        IsFull(): boolean;
        Clear(...args: any[]): any;
        Reset(...args: any[]): any;
        GetTotalWritten(...args: any[]): any;
        GetTotalRead(...args: any[]): any;
      }
      class Interleave {
        constructor(...args: any[]);
        Interleave(...args: any[]): any;
        Deinterleave(...args: any[]): any;
      }
      class Latency {
        constructor(...args: any[]);
        StartMonitoring(...args: any[]): any;
        StopMonitoring(...args: any[]): any;
        IsMonitoring(): boolean;
        RecordInputTimestamp(...args: any[]): any;
        RecordOutputTimestamp(...args: any[]): any;
        CalculateLatency(...args: any[]): any;
        GetLatencyHistory(...args: any[]): any;
        GetAverageLatency(...args: any[]): any;
        GetMaxLatency(...args: any[]): any;
        GetMinLatency(...args: any[]): any;
        GetLatencyReport(...args: any[]): any;
      }
      class Logger {
        constructor(...args: any[]);
        LogEngineStart(...args: any[]): any;
        LogEngineStop(...args: any[]): any;
        LogEngineError(...args: any[]): any;
        LogEffectLoad(...args: any[]): any;
        LogEffectProcess(...args: any[]): any;
        LogEffectError(...args: any[]): any;
        LogAnalysisStart(...args: any[]): any;
        LogAnalysisComplete(...args: any[]): any;
        LogAnalysisError(...args: any[]): any;
        LogFileLoad(...args: any[]): any;
        LogFileSave(...args: any[]): any;
        LogFileError(...args: any[]): any;
        LogBufferAllocation(...args: any[]): any;
        LogBufferDeallocation(...args: any[]): any;
        LogFFTOperation(...args: any[]): any;
        LogFilterOperation(...args: any[]): any;
      }
      class NativeBuffer {
        constructor(...args: any[]);
        Resize(...args: any[]): any;
        GetFrameCount(...args: any[]): any;
        GetSampleCount(...args: any[]): any;
        GetByteSize(...args: any[]): any;
        Clear(...args: any[]): any;
        Fill(...args: any[]): any;
        GetFloatData(...args: any[]): any;
      }
      class Resample {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        IsInitialized(): boolean;
        Convert(...args: any[]): any;
        GetRatio(...args: any[]): any;
        GetInputRate(...args: any[]): any;
        GetOutputRate(...args: any[]): any;
        Reset(...args: any[]): any;
        GetInfo(...args: any[]): any;
      }
      class SourceLoader {
        constructor(...args: any[]);
        Load(...args: any[]): any;
        DescribeOptions(...args: any[]): any;
      }
      function GetOptimalAudioCore(...args: any[]): any;
      function GetOptimalCore(...args: any[]): any;
      function GetPriorityName(...args: any[]): any;
      function SetAffinity(...args: any[]): any;
      function SetCurrentThreadPriority(...args: any[]): any;
      function SetThreadAffinity(...args: any[]): any;
    }
    namespace dsp {
      class Biquad {
        constructor(...args: any[]);
        Configure(...args: any[]): any;
        ProcessSample(...args: any[]): any;
        Process(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Convolution {
        constructor(...args: any[]);
        SetImpulseResponse(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetImpulseSize(...args: any[]): any;
        Reset(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class DelayLine {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetDelaySamples(...args: any[]): any;
        SetDelayMs(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessFractional(...args: any[]): any;
        GetMaxDelaySamples(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class FFT {
        constructor(...args: any[]);
        Forward(...args: any[]): any;
        Inverse(...args: any[]): any;
        GetSpectrum(...args: any[]): any;
        SetSpectrum(...args: any[]): any;
        GetMagnitude(...args: any[]): any;
        GetFftSize(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class FIRBank {
        constructor(...args: any[]);
        AddFilter(...args: any[]): any;
        ProcessSample(...args: any[]): any;
        GetFilterCount(...args: any[]): any;
        Clear(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class IIRBank {
        constructor(...args: any[]);
        AddBiquad(...args: any[]): any;
        ProcessSample(...args: any[]): any;
        GetFilterCount(...args: any[]): any;
        Clear(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class PhaseVocoder {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetTimeStretchRatio(...args: any[]): any;
        GetTimeStretchRatio(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class PitchShifter {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetPitchRatio(...args: any[]): any;
        SetPitchSemitones(...args: any[]): any;
        GetPitchRatio(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class SpectralShaper {
        constructor(...args: any[]);
        SetCurve(...args: any[]): any;
        GetCurve(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class STFT {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        Synthesize(...args: any[]): any;
        GetSpectrum(...args: any[]): any;
        SetSpectrum(...args: any[]): any;
        RebuildHermitianSpectrum(...args: any[]): any;
        GetFftSize(...args: any[]): any;
        GetHopSize(...args: any[]): any;
        GetLatencySamples(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      function Apply(...args: any[]): any;
      function Blackman(...args: any[]): any;
      function Describe(...args: any[]): any;
      function Hamming(...args: any[]): any;
      function Hann(...args: any[]): any;
      function SqrtHann(...args: any[]): any;
    }
    namespace fx {
      namespace custom {
        function List(...args: any[]): any;
        function RenderExample(...args: any[]): any;
        function Run(...args: any[]): any;
        function RunInterleaved(...args: any[]): any;
      }
      class AutoTune {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetCorrectionAmount(...args: any[]): any;
        SetCorrectionSpeed(...args: any[]): any;
        SetRetuneSpeedMs(...args: any[]): any;
        SetHumanize(...args: any[]): any;
        SetScaleRoot(...args: any[]): any;
        SetScaleMode(...args: any[]): any;
        SetPresenceDb(...args: any[]): any;
        SetWetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        Reset(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Bitcrush {
        constructor(...args: any[]);
        SetBitDepth(...args: any[]): any;
        SetDownsampleFactor(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Chorus {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRateHz(...args: any[]): any;
        SetDepthSamples(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Compressor {
        constructor(...args: any[]);
        SetSampleRate(...args: any[]): any;
        SetThresholdDb(...args: any[]): any;
        SetRatio(...args: any[]): any;
        SetAttackMs(...args: any[]): any;
        SetReleaseMs(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Expander {
        constructor(...args: any[]);
        SetThresholdDb(...args: any[]): any;
        SetRatio(...args: any[]): any;
        SetRangeDb(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Flanger {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRateHz(...args: any[]): any;
        SetDepthSamples(...args: any[]): any;
        SetFeedback(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Gate {
        constructor(...args: any[]);
        SetThresholdDb(...args: any[]): any;
        SetHoldSamples(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class GraphicEQ {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetBandGainDb(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Limiter {
        constructor(...args: any[]);
        SetCeilingDb(...args: any[]): any;
        SetRelease(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class ParametricEQ {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetBand(...args: any[]): any;
        GetBand(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Phaser {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRateHz(...args: any[]): any;
        SetDepth(...args: any[]): any;
        SetFeedback(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class ReverbAlgo {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRoomSize(...args: any[]): any;
        SetDamping(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class ReverbConv {
        constructor(...args: any[]);
        LoadImpulseResponse(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class RingMod {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetFrequencyHz(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Tremolo {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRateHz(...args: any[]): any;
        SetDepth(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Tube {
        constructor(...args: any[]);
        SetDrive(...args: any[]): any;
        SetOutputGain(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
      class Vibrato {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        SetRateHz(...args: any[]): any;
        SetDepthMs(...args: any[]): any;
        SetMix(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Process(...args: any[]): any;
        GetReport(...args: any[]): any;
      }
    }
    namespace midi {
      class Automation {
        constructor(...args: any[]);
        AddPoint(...args: any[]): any;
        Evaluate(...args: any[]): any;
      }
      class Clock {
        constructor(...args: any[]);
        SetTempoBpm(...args: any[]): any;
        AdvanceSamples(...args: any[]): any;
      }
      class Dispatcher {
        constructor(...args: any[]);
        AddListener(...args: any[]): any;
        Dispatch(...args: any[]): any;
        DispatchBatch(...args: any[]): any;
      }
      class Mapping {
        constructor(...args: any[]);
        Bind(...args: any[]): any;
        Resolve(...args: any[]): any;
        Clear(...args: any[]): any;
      }
      class Parser {
        constructor(...args: any[]);
        Parse(...args: any[]): any;
        DescribeType(...args: any[]): any;
      }
      class Sequencer {
        constructor(...args: any[]);
        SetPattern(...args: any[]): any;
        GetPattern(...args: any[]): any;
        SetLoopLength(...args: any[]): any;
        CollectRange(...args: any[]): any;
      }
      class Sysex {
        constructor(...args: any[]);
        IsValidSysex(): boolean;
        Parse(...args: any[]): any;
        BuildIdentityRequest(...args: any[]): any;
        BuildIdentityReply(...args: any[]): any;
      }
    }
    namespace plugin {
      class AuHost {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        LoadComponent(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        GetBackendMode(...args: any[]): any;
      }
      class BuiltinHost {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        Load(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
      }
      class ClapExternal {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        Load(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        GetLoadedIdentifier(...args: any[]): any;
      }
      class ClapHost {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        LoadPlugin(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
      }
      class ParamBridge {
        constructor(...args: any[]);
        Clear(...args: any[]): any;
        RegisterParameter(...args: any[]): any;
        SetValue(...args: any[]): any;
        SetNormalizedValue(...args: any[]): any;
        GetValue(...args: any[]): any;
        Snapshot(...args: any[]): any;
      }
      class Vst3External {
        constructor(...args: any[]);
        Load(...args: any[]): any;
        Unload(...args: any[]): any;
        IsLoaded(): boolean;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        GetPluginName(...args: any[]): any;
      }
      class Vst3Host {
        constructor(...args: any[]);
        Initialize(...args: any[]): any;
        LoadPlugin(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        GetBackendMode(...args: any[]): any;
      }
      function DefaultVst3SearchRoots(...args: any[]): any;
      function ScanVst3Plugins(...args: any[]): any;
    }
    namespace synth {
      class Adsr {
        constructor(...args: any[]);
        Set(...args: any[]): any;
        NoteOn(...args: any[]): any;
        NoteOff(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class Filter {
        constructor(...args: any[]);
        SetCutoff(...args: any[]): any;
        SetResonance(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class Lfo {
        constructor(...args: any[]);
        SetRateHz(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class ModMatrix {
        constructor(...args: any[]);
        SetAmount(...args: any[]): any;
        GetAmount(...args: any[]): any;
        SetSourceValue(...args: any[]): any;
        GetSourceValue(...args: any[]): any;
        ConfigureDestination(...args: any[]): any;
        Apply(...args: any[]): any;
        GetDestinationValue(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class MultistageEnv {
        constructor(...args: any[]);
        SetStages(...args: any[]): any;
        Trigger(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class OscAnalog {
        constructor(...args: any[]);
        SetFrequency(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class OscFm {
        constructor(...args: any[]);
        SetCarrier(...args: any[]): any;
        SetModulator(...args: any[]): any;
        SetIndex(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class OscNoise {
        constructor(...args: any[]);
        SetSeed(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class OscWavetable {
        constructor(...args: any[]);
        SetTable(...args: any[]): any;
        BuildBandlimitedTable(...args: any[]): any;
        SetFrequency(...args: any[]): any;
        Process(...args: any[]): any;
      }
      class Polyphony {
        constructor(...args: any[]);
        SetMaxVoices(...args: any[]): any;
        AllocateVoice(...args: any[]): any;
        ReleaseVoice(...args: any[]): any;
        IsAllocated(): boolean;
        GetAllocatedVoiceCount(...args: any[]): any;
      }
      class VoiceManager {
        constructor(...args: any[]);
        SetMaxVoices(...args: any[]): any;
        NoteOn(...args: any[]): any;
        NoteOff(...args: any[]): any;
        RenderSample(...args: any[]): any;
        GetActiveVoiceCount(...args: any[]): any;
        GetResidentVoiceCount(...args: any[]): any;
      }
    }
    class Buffer {
      constructor(...args: any[]);
      static FromFile(...args: any[]): any;
      static FromPCM(...args: any[]): any;
      GetSampleRate(...args: any[]): any;
      GetChannels(...args: any[]): any;
      GetFrames(...args: any[]): any;
      GetDuration(...args: any[]): any;
      GetFormat(...args: any[]): any;
      GetCodec(...args: any[]): any;
      GetBackend(...args: any[]): any;
    }
    class Player {
      constructor(...args: any[]);
      Initialize(...args: any[]): any;
      IsInitialized(): boolean;
      Load(...args: any[]): any;
      IsLoaded(): boolean;
      Play(...args: any[]): any;
      Pause(...args: any[]): any;
      Stop(...args: any[]): any;
      Seek(...args: any[]): any;
      GetState(...args: any[]): any;
      GetCurrentTime(...args: any[]): any;
      GetDuration(...args: any[]): any;
      GetVolume(...args: any[]): any;
      SetVolume(...args: any[]): any;
      GetSampleRate(...args: any[]): any;
      GetChannelCount(...args: any[]): any;
      SetFadeInOut(...args: any[]): any;
      SetOnPosition(...args: any[]): any;
      SetOnState(...args: any[]): any;
    }
    function convert(...args: any[]): any;
    function info(...args: any[]): any;
  }
  namespace image {
    function decode(...args: any[]): any;
    function info(...args: any[]): any;
    function save(...args: any[]): any;
  }
  namespace notify {
    function clear(...args: any[]): any;
    function error(...args: any[]): any;
    function info(...args: any[]): any;
    function show(...args: any[]): any;
    function success(...args: any[]): any;
    function warning(...args: any[]): any;
  }
  function info(): XERInfo;

  interface XERInfo {
    node: string;
    platform: string;
    arch: string;
    xer: string;
    [key: string]: any;
  }

  namespace UI {
    interface RenderStats {
      frames: number;
      lastFrameMs: number;
      avgFrameMs: number;
      lastDamagePct: number;
      lastFrameFull: boolean;
    }
  }

  // XER is the same object as UI at runtime (alias registered in addon.cc).
  const XER: typeof UI;
}

export = xer3;
