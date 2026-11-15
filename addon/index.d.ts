// xer3 — TypeScript definitions for the native addon (xer.node).
//
// Generated from runtime introspection of the built module: every class,
// namespace and method name below exists in xer.node. Parameter and most
// return types are intentionally `any` — refine them as the API stabilizes.
// Boolean-returning predicates (Is*/Has*/Can*) are typed.

declare namespace xer3 {
  namespace OpenAI {
    class Server {
      constructor(...args: any[]);
      isRunning(...args: any[]): any;
      on(...args: any[]): any;
      onHandleAudio(...args: any[]): any;
      onHandleCompletion(...args: any[]): any;
      onHandleImage(...args: any[]): any;
      onHandlerKey(...args: any[]): any;
      start(...args: any[]): any;
      stop(...args: any[]): any;
    }
  }
  namespace UI {
    class Accordion {
      constructor(...args: any[]);
      IsItemOpen(): boolean;
      SetItemOpen(...args: any[]): any;
      SetItems(...args: any[]): any;
      SetOnToggled(...args: any[]): any;
    }
    class Alert {
      constructor(...args: any[]);
      SetDescription(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class AlertDialog {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      SetPanelWidth(...args: any[]): any;
      SetTitle(...args: any[]): any;
      WasConfirmed(...args: any[]): any;
    }
    class Application {
      constructor(...args: any[]);
      GetAppName(...args: any[]): any;
      Initialize(...args: any[]): any;
      IsInitialized(): boolean;
      IsRunning(): boolean;
      Run(...args: any[]): any;
      SetAppName(...args: any[]): any;
      Shutdown(...args: any[]): any;
    }
    class AppTitleBar {
      constructor(...args: any[]);
      ClearActionPill(...args: any[]): any;
      ClearMenus(...args: any[]): any;
      GetCenterCommandValue(...args: any[]): any;
      GetMaximized(...args: any[]): any;
      GetWorkspaceLabel(...args: any[]): any;
      HasInteractiveAt(): boolean;
      IsInteractiveAt(): boolean;
      SetActionPill(...args: any[]): any;
      SetAvatar(...args: any[]): any;
      SetCenterCommand(...args: any[]): any;
      SetCenterCommandValue(...args: any[]): any;
      SetLeftPanelToggle(...args: any[]): any;
      SetMaximized(...args: any[]): any;
      SetMenus(...args: any[]): any;
      SetNavActions(...args: any[]): any;
      SetOnWindowCommand(...args: any[]): any;
      SetRightPanelToggle(...args: any[]): any;
      SetThemeToggle(...args: any[]): any;
      SetWorkspaceItems(...args: any[]): any;
      SetWorkspaceLabel(...args: any[]): any;
    }
    class AspectRatio {
      constructor(...args: any[]);
      GetRatio(...args: any[]): any;
      SetRatio(...args: any[]): any;
    }
    class Attachment {
      constructor(...args: any[]);
      GetFilename(...args: any[]): any;
      GetMeta(...args: any[]): any;
      SetFilename(...args: any[]): any;
      SetMeta(...args: any[]): any;
      SetOnRemove(...args: any[]): any;
    }
    class Avatar {
      constructor(...args: any[]);
      GetName(...args: any[]): any;
      SetName(...args: any[]): any;
      SetSize(...args: any[]): any;
    }
    class Badge {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      SetText(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class Breadcrumb {
      constructor(...args: any[]);
      SetItems(...args: any[]): any;
      SetOnNavigate(...args: any[]): any;
    }
    class Bubble {
      constructor(...args: any[]);
      GetAlign(...args: any[]): any;
      GetText(...args: any[]): any;
      SetAlign(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class Button {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      IsLoading(): boolean;
      SetLoading(...args: any[]): any;
      SetOnClick(...args: any[]): any;
      SetStyle(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class ButtonGroup {
      constructor(...args: any[]);
      GetOrientation(...args: any[]): any;
      SetOrientation(...args: any[]): any;
    }
    class Calendar {
      constructor(...args: any[]);
      GetDate(...args: any[]): any;
      GoToMonth(...args: any[]): any;
      SetDate(...args: any[]): any;
      SetMaxDate(...args: any[]): any;
      SetMinDate(...args: any[]): any;
      SetOnSelect(...args: any[]): any;
    }
    class Carousel {
      constructor(...args: any[]);
      AddSlide(...args: any[]): any;
      ClearSlides(...args: any[]): any;
      GetCount(...args: any[]): any;
      GetIndex(...args: any[]): any;
      Next(...args: any[]): any;
      Prev(...args: any[]): any;
      SetIndex(...args: any[]): any;
      SetLoop(...args: any[]): any;
      SetOnIndexChanged(...args: any[]): any;
      SetShowArrows(...args: any[]): any;
      SetShowDots(...args: any[]): any;
    }
    class Chart {
      constructor(...args: any[]);
      SetOnPointClick(...args: any[]): any;
      SetSeries(...args: any[]): any;
      SetShowGrid(...args: any[]): any;
      SetShowLegend(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetType(...args: any[]): any;
    }
    class Checkbox {
      constructor(...args: any[]);
      IsChecked(): boolean;
      SetChecked(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
    }
    class Collapsible {
      constructor(...args: any[]);
      GetProgress(...args: any[]): any;
      IsOpen(): boolean;
      SetOnToggled(...args: any[]): any;
      SetOpen(...args: any[]): any;
    }
    class Combobox {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      SetOnChange(...args: any[]): any;
      SetOptions(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetValue(...args: any[]): any;
    }
    class Command {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      IsOpen(): boolean;
      Open(...args: any[]): any;
      SetItems(...args: any[]): any;
      SetOnOpenChange(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
    }
    class ContextMenu {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      SetAnchor(...args: any[]): any;
    }
    class Dialog {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      SetPanelWidth(...args: any[]): any;
      SetTitle(...args: any[]): any;
      WasConfirmed(...args: any[]): any;
    }
    class Direction {
      constructor(...args: any[]);
      GetDirection(...args: any[]): any;
      SetDirection(...args: any[]): any;
    }
    class Drawer {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      SetSnapPoint(...args: any[]): any;
      SetTitle(...args: any[]): any;
    }
    class DropdownMenu {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      IsOpen(): boolean;
      Open(...args: any[]): any;
      SetItems(...args: any[]): any;
      SetOnOpenChange(...args: any[]): any;
      SetTrigger(...args: any[]): any;
    }
    class Empty {
      constructor(...args: any[]);
      GetDescription(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetDescription(...args: any[]): any;
      SetTitle(...args: any[]): any;
    }
    class Field {
      constructor(...args: any[]);
      GetDescription(...args: any[]): any;
      GetError(...args: any[]): any;
      GetLabel(...args: any[]): any;
      SetDescription(...args: any[]): any;
      SetError(...args: any[]): any;
      SetLabel(...args: any[]): any;
    }
    class Form {
      constructor(...args: any[]);
      GetColumns(...args: any[]): any;
      GetGap(...args: any[]): any;
      SetColumns(...args: any[]): any;
      SetGap(...args: any[]): any;
    }
    class HoverCard {
      constructor(...args: any[]);
      GetDescription(...args: any[]): any;
      GetTitle(...args: any[]): any;
      SetActionLabel(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetDescription(...args: any[]): any;
      SetOnAction(...args: any[]): any;
      SetOpenDelay(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetTrigger(...args: any[]): any;
    }
    class Input {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      Redo(...args: any[]): any;
      SetOnSubmit(...args: any[]): any;
      SetOnValueChanged(...args: any[]): any;
      SetPasswordMode(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetValue(...args: any[]): any;
      Undo(...args: any[]): any;
    }
    class InputGroup {
      constructor(...args: any[]);
      GetPrefix(...args: any[]): any;
      GetSuffix(...args: any[]): any;
      SetPrefix(...args: any[]): any;
      SetSuffix(...args: any[]): any;
    }
    class InputOTP {
      constructor(...args: any[]);
      GetCode(...args: any[]): any;
      GetLength(...args: any[]): any;
      SetCode(...args: any[]): any;
      SetLength(...args: any[]): any;
      SetOnComplete(...args: any[]): any;
    }
    class Item {
      constructor(...args: any[]);
      GetDescription(...args: any[]): any;
      GetTitle(...args: any[]): any;
      IsSelected(): boolean;
      SetDescription(...args: any[]): any;
      SetSelected(...args: any[]): any;
      SetTitle(...args: any[]): any;
    }
    class Kbd {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class Label {
      constructor(...args: any[]);
      AddChild(...args: any[]): any;
      ClipsChildren(): boolean;
      DumpXml(): string;
      GetBounds(...args: any[]): any;
      GetChildren(...args: any[]): any;
      GetContentHeight(...args: any[]): any;
      GetMaxSize(...args: any[]): any;
      GetMinSize(...args: any[]): any;
      GetParent(...args: any[]): any;
      GetPreferredSize(...args: any[]): any;
      GetText(...args: any[]): any;
      GetTooltip(...args: any[]): any;
      GetVisibleRect(...args: any[]): any;
      HideOverlay(...args: any[]): any;
      Invalidate(...args: any[]): any;
      IsEffectivelyVisible(): boolean;
      IsEnabled(): boolean;
      IsFocusable(): boolean;
      IsFocused(): boolean;
      IsHovered(): boolean;
      IsVisible(): boolean;
      RemoveChild(...args: any[]): any;
      SetBounds(...args: any[]): any;
      SetClipsChildren(...args: any[]): any;
      SetEnabled(...args: any[]): any;
      SetFocusable(...args: any[]): any;
      SetFocused(...args: any[]): any;
      SetHovered(...args: any[]): any;
      SetParent(...args: any[]): any;
      SetPosition(...args: any[]): any;
      SetSize(...args: any[]): any;
      SetText(...args: any[]): any;
      SetTooltip(...args: any[]): any;
      SetVisible(...args: any[]): any;
      SetVisibleRect(...args: any[]): any;
      ShowOverlay(...args: any[]): any;
      Tick(...args: any[]): any;
      TickTree(...args: any[]): any;
    }
    class Marker {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class Message {
      constructor(...args: any[]);
      GetAlign(...args: any[]): any;
      GetSender(...args: any[]): any;
      GetText(...args: any[]): any;
      GetTime(...args: any[]): any;
      SetAlign(...args: any[]): any;
      SetSender(...args: any[]): any;
      SetText(...args: any[]): any;
      SetTime(...args: any[]): any;
    }
    class MessageScroller {
      constructor(...args: any[]);
      AddMessage(...args: any[]): any;
      Clear(...args: any[]): any;
      GetScrollY(...args: any[]): any;
      ScrollToBottom(...args: any[]): any;
    }
    class NativeSelect {
      constructor(...args: any[]);
      GetSelectedIndex(...args: any[]): any;
      GetSelectedText(...args: any[]): any;
      IsDropdownOpen(): boolean;
      SetOnChanged(...args: any[]): any;
      SetOptions(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
    }
    class Pagination {
      constructor(...args: any[]);
      GetCurrentPage(...args: any[]): any;
      SetCurrentPage(...args: any[]): any;
      SetOnPageChanged(...args: any[]): any;
      SetPageCount(...args: any[]): any;
      VisiblePages(...args: any[]): any;
    }
    class Popover {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      GetTitle(...args: any[]): any;
      IsOpen(): boolean;
      Open(...args: any[]): any;
      SetAlign(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOnOpenChange(...args: any[]): any;
      SetOpen(...args: any[]): any;
      SetSide(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetTrigger(...args: any[]): any;
    }
    class Progress {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      SetValue(...args: any[]): any;
    }
    class RadioGroup {
      constructor(...args: any[]);
      GetSelectedIndex(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
      SetOptions(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
    }
    class Resizable {
      constructor(...args: any[]);
      AddPanel(...args: any[]): any;
      ClearPanels(...args: any[]): any;
      SetCollapsed(...args: any[]): any;
      SetDirection(...args: any[]): any;
      SetHandleSize(...args: any[]): any;
      SetOnLayout(...args: any[]): any;
    }
    class ScreenLoadingWidget {
      constructor(...args: any[]);
      IsFinished(): boolean;
      Start(...args: any[]): any;
      Tick(...args: any[]): any;
    }
    class ScrollArea {
      constructor(...args: any[]);
      GetContent(...args: any[]): any;
      GetScrollRange(...args: any[]): any;
      GetScrollY(...args: any[]): any;
      ScrollTo(...args: any[]): any;
      SetContent(...args: any[]): any;
    }
    class Select {
      constructor(...args: any[]);
      GetSelectedIndex(...args: any[]): any;
      GetSelectedText(...args: any[]): any;
      IsDropdownOpen(): boolean;
      SetOnChanged(...args: any[]): any;
      SetOptions(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetSelectedIndex(...args: any[]): any;
    }
    class Sheet {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetOnClosed(...args: any[]): any;
      SetPanelSize(...args: any[]): any;
      SetSide(...args: any[]): any;
      SetTitle(...args: any[]): any;
    }
    class Sidebar {
      constructor(...args: any[]);
      GetActiveIndex(...args: any[]): any;
      SetActiveIndex(...args: any[]): any;
      SetCollapsed(...args: any[]): any;
      SetHeader(...args: any[]): any;
      SetItems(...args: any[]): any;
      SetOnSelect(...args: any[]): any;
    }
    class Skeleton {
      constructor(...args: any[]);
      SetSize(...args: any[]): any;
      SetVariant(...args: any[]): any;
    }
    class Slider {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      IsDragging(): boolean;
      SetOnValueChanged(...args: any[]): any;
      SetRange(...args: any[]): any;
      SetValue(...args: any[]): any;
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
      GetSelectedRow(...args: any[]): any;
      ScrollTo(...args: any[]): any;
      SetColumns(...args: any[]): any;
      SetHeader(...args: any[]): any;
      SetOnCellClick(...args: any[]): any;
      SetOnRowClick(...args: any[]): any;
      SetRowHeight(...args: any[]): any;
      SetRows(...args: any[]): any;
      SetSelectedRow(...args: any[]): any;
    }
    class Tabs {
      constructor(...args: any[]);
      AddTab(...args: any[]): any;
      ClearTabs(...args: any[]): any;
      GetActiveIndex(...args: any[]): any;
      GetStyle(...args: any[]): any;
      SetActiveIndex(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
      SetStyle(...args: any[]): any;
    }
    class Textarea {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      SetOnChanged(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetReadOnly(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class TextInput {
      constructor(...args: any[]);
      GetValue(...args: any[]): any;
      Redo(...args: any[]): any;
      SetOnSubmit(...args: any[]): any;
      SetOnValueChanged(...args: any[]): any;
      SetPasswordMode(...args: any[]): any;
      SetPlaceholder(...args: any[]): any;
      SetValue(...args: any[]): any;
      Undo(...args: any[]): any;
    }
    class Toggle {
      constructor(...args: any[]);
      GetText(...args: any[]): any;
      IsPressed(): boolean;
      SetOnPressedChanged(...args: any[]): any;
      SetPressed(...args: any[]): any;
      SetText(...args: any[]): any;
    }
    class ToggleGroup {
      constructor(...args: any[]);
      GetSelectedIndices(...args: any[]): any;
      IsSelected(): boolean;
      SetOnChanged(...args: any[]): any;
      SetOptions(...args: any[]): any;
      SetSelectedIndices(...args: any[]): any;
    }
    class Tooltip {
      constructor(...args: any[]);
      SetAnchor(...args: any[]): any;
    }
    class WaveformWidget {
      constructor(...args: any[]);
      SetPlaying(...args: any[]): any;
      SetProgress(...args: any[]): any;
    }
    class Widget {
      constructor(...args: any[]);
      AddChild(...args: any[]): any;
      ClipsChildren(): boolean;
      DumpXml(): string;
      GetBounds(...args: any[]): any;
      GetChildren(...args: any[]): any;
      GetContentHeight(): number;
      GetMaxSize(...args: any[]): any;
      GetMinSize(...args: any[]): any;
      GetParent(...args: any[]): any;
      GetPreferredSize(...args: any[]): any;
      GetTooltip(): string;
      GetVisibleRect(...args: any[]): any;
      HideOverlay(...args: any[]): any;
      Invalidate(...args: any[]): any;
      IsEffectivelyVisible(): boolean;
      IsEnabled(): boolean;
      IsFocusable(): boolean;
      IsFocused(): boolean;
      IsHovered(): boolean;
      IsVisible(): boolean;
      RemoveChild(...args: any[]): any;
      SetBounds(...args: any[]): any;
      SetClipsChildren(...args: any[]): any;
      SetEnabled(...args: any[]): any;
      SetFocusable(...args: any[]): any;
      SetFocused(...args: any[]): any;
      SetHovered(...args: any[]): any;
      SetParent(...args: any[]): any;
      SetPosition(...args: any[]): any;
      SetSize(...args: any[]): any;
      SetTooltip(...args: any[]): any;
      SetVisible(...args: any[]): any;
      SetVisibleRect(...args: any[]): any;
      ShowOverlay(...args: any[]): any;
      Tick(...args: any[]): any;
      TickTree(...args: any[]): any;
    }
    class Window {
      constructor(...args: any[]);
      Close(...args: any[]): any;
      Create(...args: any[]): any;
      GetContent(...args: any[]): any;
      GetHWND(): number;
      GetLastRenderError(): string;
      GetPosition(...args: any[]): any;
      GetRenderStats(): RenderStats;
      GetSize(...args: any[]): any;
      GetTheme(): string;
      GetTitle(): string;
      HasCustomTitleBar(): boolean;
      Hide(...args: any[]): any;
      Invalidate(...args: any[]): any;
      IsClosed(): boolean;
      IsVisible(): boolean;
      IsWindowMaximized(): boolean;
      Minimize(...args: any[]): any;
      OpenDevTools(): boolean;
      SetCloseHides(...args: any[]): any;
      SetContent(...args: any[]): any;
      SetCustomTitleBar(...args: any[]): any;
      SetFrameCallback(...args: any[]): any;
      SetOnClose(...args: any[]): any;
      SetOnResize(...args: any[]): any;
      SetPosition(...args: any[]): any;
      SetSize(...args: any[]): any;
      SetSizeLimits(...args: any[]): any;
      SetTheme(...args: any[]): any;
      SetTitle(...args: any[]): any;
      SetTitleBarHitTest(...args: any[]): any;
      Show(...args: any[]): any;
      ToggleMaximize(...args: any[]): any;
    }
    function parseXml(markup: string): XmlLoadResult;
    function parseXmlFile(path: string): XmlLoadResult;
    function Separator(...args: any[]): any;
    function xmlSupportedTags(): string;
  }
  namespace audio {
    namespace analysis {
      class BeatTracker {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        GetAverageStrength(...args: any[]): any;
        GetBeatEvents(...args: any[]): any;
        GetEstimatedBPM(...args: any[]): any;
        GetReport(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class FFTAnalyzer {
        constructor(...args: any[]);
        AnalyzeSpectrum(...args: any[]): any;
        ComputeFFT(...args: any[]): any;
        GetAnalysisReport(...args: any[]): any;
        GetDominantFrequency(...args: any[]): any;
        GetFFTOutput(...args: any[]): any;
        GetFrequencyBands(...args: any[]): any;
        GetMagnitudeSpectrum(...args: any[]): any;
        GetPhaseSpectrum(...args: any[]): any;
        GetRMSEnergy(...args: any[]): any;
        GetTotalPower(...args: any[]): any;
      }
      class HarmonicContent {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        GetFundamentalFrequency(...args: any[]): any;
        GetHarmonicRatio(...args: any[]): any;
        GetHarmonicSeries(...args: any[]): any;
        GetInharmonicity(...args: any[]): any;
        GetSummary(...args: any[]): any;
      }
      class LoudnessMeter {
        constructor(...args: any[]);
        GetHistory(...args: any[]): any;
        GetIntegratedLUFS(...args: any[]): any;
        GetMomentaryLUFS(...args: any[]): any;
        GetProcessedBlocks(...args: any[]): any;
        GetReport(...args: any[]): any;
        GetShortTermLUFS(...args: any[]): any;
        GetTruePeakDBFS(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class OnsetDetector {
        constructor(...args: any[]);
        DetectOnsets(...args: any[]): any;
        GetAverageStrength(...args: any[]): any;
        GetFluxCurve(...args: any[]): any;
        GetOnsetFrames(...args: any[]): any;
        GetOnsetTimesSeconds(...args: any[]): any;
        GetReport(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class PitchDetector {
        constructor(...args: any[]);
        DetectPitch(...args: any[]): any;
        FrequencyToNote(...args: any[]): any;
        GetConfidence(...args: any[]): any;
        GetFundamentalFrequency(...args: any[]): any;
        GetHarmonics(...args: any[]): any;
        GetSampleRate(...args: any[]): any;
        IsPitched(): boolean;
      }
      class PitchEstimator {
        constructor(...args: any[]);
        Estimate(...args: any[]): any;
        GetLastHarmonics(...args: any[]): any;
        GetLastNote(...args: any[]): any;
        GetLastPitch(...args: any[]): any;
        GetReport(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class RmsPeak {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        GetCrestFactor(...args: any[]): any;
        GetPeak(...args: any[]): any;
        GetPeakDBFS(...args: any[]): any;
        GetRMS(...args: any[]): any;
        GetReport(...args: any[]): any;
        GetWindowHistory(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class Spectrogram {
        constructor(...args: any[]);
        ExportAsRaw(...args: any[]): any;
        Generate(...args: any[]): any;
        GetBinCount(...args: any[]): any;
        GetFrameCount(...args: any[]): any;
        GetMagnitudeMatrix(...args: any[]): any;
        GetReport(...args: any[]): any;
        Reset(...args: any[]): any;
      }
    }
    namespace codec {
      class Aac {
        constructor(...args: any[]);
        Decode(...args: any[]): any;
        Encode(...args: any[]): any;
      }
      class FileStreamer {
        constructor(...args: any[]);
        Close(...args: any[]): any;
        OpenRead(...args: any[]): any;
        OpenWrite(...args: any[]): any;
        ReadBytes(...args: any[]): any;
        WriteBytes(...args: any[]): any;
      }
      class Flac {
        constructor(...args: any[]);
        Decode(...args: any[]): any;
      }
      class Id3 {
        constructor(...args: any[]);
        Build(...args: any[]): any;
        Parse(...args: any[]): any;
      }
      class Mp3 {
        constructor(...args: any[]);
        Decode(...args: any[]): any;
        Encode(...args: any[]): any;
        EncodeInterleaved(...args: any[]): any;
      }
      class Ogg {
        constructor(...args: any[]);
        Decode(...args: any[]): any;
        Encode(...args: any[]): any;
      }
      class WavFloat {
        constructor(...args: any[]);
        Decode32(...args: any[]): any;
        Encode32(...args: any[]): any;
      }
      class WavPcm {
        constructor(...args: any[]);
        Decode16(...args: any[]): any;
        Encode16(...args: any[]): any;
      }
    }
    namespace core {
      class AsyncManager {
        constructor(...args: any[]);
        GetPendingOperationCount(...args: any[]): any;
        IsRunning(): boolean;
        Start(...args: any[]): any;
        Stop(...args: any[]): any;
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
        Clear(...args: any[]): any;
        GetAvailableBuffers(...args: any[]): any;
        GetPoolStatus(...args: any[]): any;
        GetTotalBuffers(...args: any[]): any;
        ReleaseBuffer(...args: any[]): any;
        Resize(...args: any[]): any;
      }
      class CacheManager {
        constructor(...args: any[]);
        CacheAnalysisResult(...args: any[]): any;
        CacheAudioMetadata(...args: any[]): any;
        CacheEffectPreset(...args: any[]): any;
        ClearAnalysisCache(...args: any[]): any;
        ClearMetadataCache(...args: any[]): any;
        ClearPresetCache(...args: any[]): any;
        GetAnalysisCount(...args: any[]): any;
        GetCachedAnalysisResult(...args: any[]): any;
        GetCachedMetadata(...args: any[]): any;
        GetCachedPreset(...args: any[]): any;
        GetDefaultAnalysisTtl(...args: any[]): any;
        GetDefaultMetadataTtl(...args: any[]): any;
        GetDefaultPresetTtl(...args: any[]): any;
        GetMetadataCount(...args: any[]): any;
        GetPresetCount(...args: any[]): any;
        InvalidateAnalysisResult(...args: any[]): any;
        InvalidateMetadata(...args: any[]): any;
        InvalidatePreset(...args: any[]): any;
        SetDefaultAnalysisTtl(...args: any[]): any;
        SetDefaultMetadataTtl(...args: any[]): any;
        SetDefaultPresetTtl(...args: any[]): any;
      }
      class ClockSync {
        constructor(...args: any[]);
        CalculateSampleRateRatio(...args: any[]): any;
        CheckClockLock(...args: any[]): any;
        GetClockInfo(...args: any[]): any;
        GetCurrentFrame(...args: any[]): any;
        GetCurrentTime(...args: any[]): any;
        GetDriftPercentage(...args: any[]): any;
        Initialize(...args: any[]): any;
        IsInitialized(): boolean;
        SyncToExternal(...args: any[]): any;
        UpdateClock(...args: any[]): any;
      }
      class Engine {
        constructor(...args: any[]);
        GetBufferSize(...args: any[]): any;
        GetChannelCount(...args: any[]): any;
        GetSampleRate(...args: any[]): any;
        Initialize(...args: any[]): any;
        IsInitialized(): boolean;
        IsRunning(): boolean;
        SetBufferSize(...args: any[]): any;
        SetSampleRate(...args: any[]): any;
        Start(...args: any[]): any;
        Stop(...args: any[]): any;
      }
      class EngineHost {
        constructor(...args: any[]);
        CreateEngine(...args: any[]): any;
        DestroyEngine(...args: any[]): any;
        GetEngineCount(...args: any[]): any;
        GetEngineList(...args: any[]): any;
        GetHostInfo(...args: any[]): any;
        SetGlobalBufferSize(...args: any[]): any;
        SetGlobalSampleRate(...args: any[]): any;
        StartAllEngines(...args: any[]): any;
        StopAllEngines(...args: any[]): any;
      }
      class Fifo {
        constructor(...args: any[]);
        Clear(...args: any[]): any;
        GetAvailableFrames(...args: any[]): any;
        GetCapacity(...args: any[]): any;
        GetTotalRead(...args: any[]): any;
        GetTotalWritten(...args: any[]): any;
        IsEmpty(): boolean;
        IsFull(): boolean;
        Read(...args: any[]): any;
        Reset(...args: any[]): any;
        Write(...args: any[]): any;
      }
      class Interleave {
        constructor(...args: any[]);
        Deinterleave(...args: any[]): any;
        Interleave(...args: any[]): any;
      }
      class Latency {
        constructor(...args: any[]);
        CalculateLatency(...args: any[]): any;
        GetAverageLatency(...args: any[]): any;
        GetLatencyHistory(...args: any[]): any;
        GetLatencyReport(...args: any[]): any;
        GetMaxLatency(...args: any[]): any;
        GetMinLatency(...args: any[]): any;
        IsMonitoring(): boolean;
        RecordInputTimestamp(...args: any[]): any;
        RecordOutputTimestamp(...args: any[]): any;
        StartMonitoring(...args: any[]): any;
        StopMonitoring(...args: any[]): any;
      }
      class Logger {
        constructor(...args: any[]);
        LogAnalysisComplete(...args: any[]): any;
        LogAnalysisError(...args: any[]): any;
        LogAnalysisStart(...args: any[]): any;
        LogBufferAllocation(...args: any[]): any;
        LogBufferDeallocation(...args: any[]): any;
        LogEffectError(...args: any[]): any;
        LogEffectLoad(...args: any[]): any;
        LogEffectProcess(...args: any[]): any;
        LogEngineError(...args: any[]): any;
        LogEngineStart(...args: any[]): any;
        LogEngineStop(...args: any[]): any;
        LogFFTOperation(...args: any[]): any;
        LogFileError(...args: any[]): any;
        LogFileLoad(...args: any[]): any;
        LogFileSave(...args: any[]): any;
        LogFilterOperation(...args: any[]): any;
      }
      class NativeBuffer {
        constructor(...args: any[]);
        Clear(...args: any[]): any;
        Fill(...args: any[]): any;
        GetByteSize(...args: any[]): any;
        GetFloatData(...args: any[]): any;
        GetFrameCount(...args: any[]): any;
        GetSampleCount(...args: any[]): any;
        Resize(...args: any[]): any;
      }
      class Resample {
        constructor(...args: any[]);
        Convert(...args: any[]): any;
        GetInfo(...args: any[]): any;
        GetInputRate(...args: any[]): any;
        GetOutputRate(...args: any[]): any;
        GetRatio(...args: any[]): any;
        Initialize(...args: any[]): any;
        IsInitialized(): boolean;
        Reset(...args: any[]): any;
      }
      class SourceLoader {
        constructor(...args: any[]);
        DescribeOptions(...args: any[]): any;
        Load(...args: any[]): any;
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
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessSample(...args: any[]): any;
        Reset(...args: any[]): any;
      }
      class Convolution {
        constructor(...args: any[]);
        GetImpulseSize(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        SetImpulseResponse(...args: any[]): any;
      }
      class DelayLine {
        constructor(...args: any[]);
        GetMaxDelaySamples(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessFractional(...args: any[]): any;
        Reset(...args: any[]): any;
        SetDelayMs(...args: any[]): any;
        SetDelaySamples(...args: any[]): any;
      }
      class FFT {
        constructor(...args: any[]);
        Forward(...args: any[]): any;
        GetFftSize(...args: any[]): any;
        GetMagnitude(...args: any[]): any;
        GetReport(...args: any[]): any;
        GetSpectrum(...args: any[]): any;
        Inverse(...args: any[]): any;
        Reset(...args: any[]): any;
        SetSpectrum(...args: any[]): any;
      }
      class FIRBank {
        constructor(...args: any[]);
        AddFilter(...args: any[]): any;
        Clear(...args: any[]): any;
        GetFilterCount(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        ProcessSample(...args: any[]): any;
      }
      class IIRBank {
        constructor(...args: any[]);
        AddBiquad(...args: any[]): any;
        Clear(...args: any[]): any;
        GetFilterCount(...args: any[]): any;
        GetReport(...args: any[]): any;
        ProcessSample(...args: any[]): any;
      }
      class PhaseVocoder {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        GetTimeStretchRatio(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        SetTimeStretchRatio(...args: any[]): any;
      }
      class PitchShifter {
        constructor(...args: any[]);
        GetPitchRatio(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        SetPitchRatio(...args: any[]): any;
        SetPitchSemitones(...args: any[]): any;
      }
      class SpectralShaper {
        constructor(...args: any[]);
        GetCurve(...args: any[]): any;
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        SetCurve(...args: any[]): any;
      }
      class STFT {
        constructor(...args: any[]);
        Analyze(...args: any[]): any;
        GetFftSize(...args: any[]): any;
        GetHopSize(...args: any[]): any;
        GetLatencySamples(...args: any[]): any;
        GetReport(...args: any[]): any;
        GetSpectrum(...args: any[]): any;
        RebuildHermitianSpectrum(...args: any[]): any;
        Reset(...args: any[]): any;
        SetSpectrum(...args: any[]): any;
        Synthesize(...args: any[]): any;
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
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        Reset(...args: any[]): any;
        SetCorrectionAmount(...args: any[]): any;
        SetCorrectionSpeed(...args: any[]): any;
        SetHumanize(...args: any[]): any;
        SetPresenceDb(...args: any[]): any;
        SetRetuneSpeedMs(...args: any[]): any;
        SetScaleMode(...args: any[]): any;
        SetScaleRoot(...args: any[]): any;
        SetWetMix(...args: any[]): any;
      }
      class Bitcrush {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetBitDepth(...args: any[]): any;
        SetDownsampleFactor(...args: any[]): any;
      }
      class Chorus {
        constructor(...args: any[]);
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDepthSamples(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRateHz(...args: any[]): any;
      }
      class Compressor {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetAttackMs(...args: any[]): any;
        SetRatio(...args: any[]): any;
        SetReleaseMs(...args: any[]): any;
        SetSampleRate(...args: any[]): any;
        SetThresholdDb(...args: any[]): any;
      }
      class Expander {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetRangeDb(...args: any[]): any;
        SetRatio(...args: any[]): any;
        SetThresholdDb(...args: any[]): any;
      }
      class Flanger {
        constructor(...args: any[]);
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDepthSamples(...args: any[]): any;
        SetFeedback(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRateHz(...args: any[]): any;
      }
      class Gate {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetHoldSamples(...args: any[]): any;
        SetThresholdDb(...args: any[]): any;
      }
      class GraphicEQ {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetBandGainDb(...args: any[]): any;
      }
      class Limiter {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetCeilingDb(...args: any[]): any;
        SetRelease(...args: any[]): any;
      }
      class ParametricEQ {
        constructor(...args: any[]);
        GetBand(...args: any[]): any;
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetBand(...args: any[]): any;
      }
      class Phaser {
        constructor(...args: any[]);
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDepth(...args: any[]): any;
        SetFeedback(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRateHz(...args: any[]): any;
      }
      class ReverbAlgo {
        constructor(...args: any[]);
        GetMemoryStats(...args: any[]): any;
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDamping(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRoomSize(...args: any[]): any;
      }
      class ReverbConv {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        LoadImpulseResponse(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetMix(...args: any[]): any;
      }
      class RingMod {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetFrequencyHz(...args: any[]): any;
        SetMix(...args: any[]): any;
      }
      class Tremolo {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDepth(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRateHz(...args: any[]): any;
      }
      class Tube {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDrive(...args: any[]): any;
        SetOutputGain(...args: any[]): any;
      }
      class Vibrato {
        constructor(...args: any[]);
        GetReport(...args: any[]): any;
        Initialize(...args: any[]): any;
        Process(...args: any[]): any;
        ProcessBlock(...args: any[]): any;
        SetDepthMs(...args: any[]): any;
        SetMix(...args: any[]): any;
        SetRateHz(...args: any[]): any;
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
        AdvanceSamples(...args: any[]): any;
        SetTempoBpm(...args: any[]): any;
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
        Clear(...args: any[]): any;
        Resolve(...args: any[]): any;
      }
      class Parser {
        constructor(...args: any[]);
        DescribeType(...args: any[]): any;
        Parse(...args: any[]): any;
      }
      class Sequencer {
        constructor(...args: any[]);
        CollectRange(...args: any[]): any;
        GetPattern(...args: any[]): any;
        SetLoopLength(...args: any[]): any;
        SetPattern(...args: any[]): any;
      }
      class Sysex {
        constructor(...args: any[]);
        BuildIdentityReply(...args: any[]): any;
        BuildIdentityRequest(...args: any[]): any;
        IsValidSysex(): boolean;
        Parse(...args: any[]): any;
      }
    }
    namespace plugin {
      class AuHost {
        constructor(...args: any[]);
        GetBackendMode(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        Initialize(...args: any[]): any;
        LoadComponent(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
      }
      class BuiltinHost {
        constructor(...args: any[]);
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        Initialize(...args: any[]): any;
        Load(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
      }
      class ClapExternal {
        constructor(...args: any[]);
        GetLoadedIdentifier(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        Initialize(...args: any[]): any;
        Load(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
      }
      class ClapHost {
        constructor(...args: any[]);
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        Initialize(...args: any[]): any;
        LoadPlugin(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
      }
      class ParamBridge {
        constructor(...args: any[]);
        Clear(...args: any[]): any;
        GetValue(...args: any[]): any;
        RegisterParameter(...args: any[]): any;
        SetNormalizedValue(...args: any[]): any;
        SetValue(...args: any[]): any;
        Snapshot(...args: any[]): any;
      }
      class Vst3External {
        constructor(...args: any[]);
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        GetPluginName(...args: any[]): any;
        IsLoaded(): boolean;
        Load(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
        Unload(...args: any[]): any;
      }
      class Vst3Host {
        constructor(...args: any[]);
        GetBackendMode(...args: any[]): any;
        GetParameter(...args: any[]): any;
        GetParameters(...args: any[]): any;
        Initialize(...args: any[]): any;
        LoadPlugin(...args: any[]): any;
        Process(...args: any[]): any;
        SetParameter(...args: any[]): any;
      }
      function DefaultVst3SearchRoots(...args: any[]): any;
      function ScanVst3Plugins(...args: any[]): any;
    }
    namespace synth {
      class Adsr {
        constructor(...args: any[]);
        NoteOff(...args: any[]): any;
        NoteOn(...args: any[]): any;
        Process(...args: any[]): any;
        Set(...args: any[]): any;
      }
      class Filter {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetCutoff(...args: any[]): any;
        SetResonance(...args: any[]): any;
      }
      class Lfo {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetRateHz(...args: any[]): any;
      }
      class ModMatrix {
        constructor(...args: any[]);
        Apply(...args: any[]): any;
        ConfigureDestination(...args: any[]): any;
        GetAmount(...args: any[]): any;
        GetDestinationValue(...args: any[]): any;
        GetSourceValue(...args: any[]): any;
        Reset(...args: any[]): any;
        SetAmount(...args: any[]): any;
        SetSourceValue(...args: any[]): any;
      }
      class MultistageEnv {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetStages(...args: any[]): any;
        Trigger(...args: any[]): any;
      }
      class OscAnalog {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetFrequency(...args: any[]): any;
      }
      class OscFm {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetCarrier(...args: any[]): any;
        SetIndex(...args: any[]): any;
        SetModulator(...args: any[]): any;
      }
      class OscNoise {
        constructor(...args: any[]);
        Process(...args: any[]): any;
        SetSeed(...args: any[]): any;
      }
      class OscWavetable {
        constructor(...args: any[]);
        BuildBandlimitedTable(...args: any[]): any;
        Process(...args: any[]): any;
        SetFrequency(...args: any[]): any;
        SetTable(...args: any[]): any;
      }
      class Polyphony {
        constructor(...args: any[]);
        AllocateVoice(...args: any[]): any;
        GetAllocatedVoiceCount(...args: any[]): any;
        IsAllocated(): boolean;
        ReleaseVoice(...args: any[]): any;
        SetMaxVoices(...args: any[]): any;
      }
      class VoiceManager {
        constructor(...args: any[]);
        GetActiveVoiceCount(...args: any[]): any;
        GetResidentVoiceCount(...args: any[]): any;
        NoteOff(...args: any[]): any;
        NoteOn(...args: any[]): any;
        RenderSample(...args: any[]): any;
        SetMaxVoices(...args: any[]): any;
      }
    }
    class Buffer {
      constructor(...args: any[]);
      static FromFile(...args: any[]): any;
      static FromPCM(...args: any[]): any;
      GetBackend(...args: any[]): any;
      GetChannels(...args: any[]): any;
      GetCodec(...args: any[]): any;
      GetDuration(...args: any[]): any;
      GetFormat(...args: any[]): any;
      GetFrames(...args: any[]): any;
      GetSampleRate(...args: any[]): any;
    }
    class Player {
      constructor(...args: any[]);
      GetChannelCount(...args: any[]): any;
      GetCurrentTime(...args: any[]): any;
      GetDuration(...args: any[]): any;
      GetSampleRate(...args: any[]): any;
      GetState(...args: any[]): any;
      GetVolume(...args: any[]): any;
      Initialize(...args: any[]): any;
      IsInitialized(): boolean;
      IsLoaded(): boolean;
      Load(...args: any[]): any;
      Pause(...args: any[]): any;
      Play(...args: any[]): any;
      Seek(...args: any[]): any;
      SetFadeInOut(...args: any[]): any;
      SetOnPosition(...args: any[]): any;
      SetOnState(...args: any[]): any;
      SetVolume(...args: any[]): any;
      Stop(...args: any[]): any;
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
    interface XmlLoadResult {
      ok: boolean;
      widget?: Widget;
      warnings: string[];
      error: string;
    }
  }

  // XER is the same object as UI at runtime (alias registered in addon.cc).
  const XER: typeof UI;
}

export = xer3;
