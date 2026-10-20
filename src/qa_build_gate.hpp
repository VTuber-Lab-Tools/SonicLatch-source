#pragma once
#ifndef SONICLATCH_ENABLE_EMBEDDED_QA
#define SONICLATCH_ENABLE_EMBEDDED_QA 0
#endif
#if defined(SONICLATCH_ISOLATED_POSITIVE_FIXTURE) && !SONICLATCH_ENABLE_EMBEDDED_QA
#error Positive fixture requires embedded QA
#endif
#if SONICLATCH_ENABLE_EMBEDDED_QA
#include <qa-local-paths.hpp>
#define SL_QA_ENV(name) qgetenv(name)
#else
#define SL_QA_ENV(name) QByteArray{}
#endif
