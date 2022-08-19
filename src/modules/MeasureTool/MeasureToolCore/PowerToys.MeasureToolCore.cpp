﻿#include "pch.h"

#include <common/display/dpi_aware.h>
#include <common/utils/logger_helper.h>
#include <common/logger/logger.h>

#include "constants.h"
#include "PowerToys.MeasureToolCore.h"
#include "Core.g.cpp"
#include "OverlayUI.h"
#include "ScreenCapturing.h"

namespace winrt::PowerToys::MeasureToolCore::implementation
{
    void Core::MouseCaptureThread()
    {
        while (!_stopMouseCaptureThreadSignal.is_signaled())
        {
            static_assert(sizeof(_commonState.cursorPos) == sizeof(LONG64));
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            InterlockedExchange64(reinterpret_cast<LONG64*>(&_commonState.cursorPos), std::bit_cast<LONG64>(cursorPos));
            std::this_thread::sleep_for(consts::TARGET_FRAME_DURATION);
        }
    }

    Core::Core() :
        _mouseCaptureThread{ [this] { MouseCaptureThread(); } },
        _stopMouseCaptureThreadSignal{ wil::EventOptions::ManualReset }
    {
        LoggerHelpers::init_logger(L"Measure Tool", L"Core", "Measure Tool");
    }

    Core::~Core()
    {
        _stopMouseCaptureThreadSignal.SetEvent();
        _mouseCaptureThread.join();
    }

    void Core::ResetState()
    {
        _overlayUIState = {};
        _boundsToolState = { .commonState = &_commonState };
        _measureToolState.Reset();
        if (_screenCaptureThread.joinable())
        {
            _screenCaptureThread.join();
        }

        _settings = Settings::LoadFromFile();

        _commonState.lineColor.r = _settings.lineColor[0] / 255.f;
        _commonState.lineColor.g = _settings.lineColor[1] / 255.f;
        _commonState.lineColor.b = _settings.lineColor[2] / 255.f;
    }

    void Core::StartBoundsTool()
    {
        ResetState();

        const auto primaryMonitor = MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY);
        _overlayUIState = OverlayUIState::Create(_boundsToolState, _commonState, primaryMonitor);
    }

    void Core::StartMeasureTool(const bool horizontal, const bool vertical)
    {
        ResetState();

        _measureToolState.Access([horizontal, vertical, this](MeasureToolState& state) {
            if (horizontal)
                state.mode = vertical ? MeasureToolState::Mode::Cross : MeasureToolState::Mode::Horizontal;
            else
                state.mode = MeasureToolState::Mode::Vertical;

            state.continuousCapture = _settings.continuousCapture;
            state.drawFeetOnCross = _settings.drawFeetOnCross;
            state.pixelTolerance = _settings.pixelTolerance;
        });

        const auto primaryMonitor = MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY);
        _overlayUIState = OverlayUIState::Create(_measureToolState, _commonState, primaryMonitor);
        if (_overlayUIState)
        {
            _screenCaptureThread = StartCapturingThread(_commonState, _measureToolState, _overlayUIState->overlayWindowHandle(), primaryMonitor);
        }
    }

    void MeasureToolCore::implementation::Core::SetToolCompletionEvent(ToolSessionCompleted sessionCompletedTrigger)
    {
        _commonState.sessionCompletedCallback = [trigger = std::move(sessionCompletedTrigger)] {
            trigger();
        };
    }

    void MeasureToolCore::implementation::Core::SetToolbarBoundingBox(const uint32_t fromX,
                                                                      const uint32_t fromY,
                                                                      const uint32_t toX,
                                                                      const uint32_t toY)
    {
        _commonState.toolbarBoundingBox.left = fromX;
        _commonState.toolbarBoundingBox.right = toX;
        _commonState.toolbarBoundingBox.top = fromY;
        _commonState.toolbarBoundingBox.bottom = toY;
    }

    float MeasureToolCore::implementation::Core::GetDPIScaleForWindow(uint64_t windowHandle)
    {
        UINT dpi = DPIAware::DEFAULT_DPI;
        DPIAware::GetScreenDPIForWindow(std::bit_cast<HWND>(windowHandle), dpi);
        return static_cast<float>(dpi) / DPIAware::DEFAULT_DPI;
    }
}