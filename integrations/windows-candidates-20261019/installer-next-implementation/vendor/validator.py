#!/usr/bin/env python3
"""Read-only ZIP inventory/release-gate validator. Python 3.10+, standard library.

Never installs, executes, extracts, removes, or changes candidate files. A passed
check is structural evidence for human review, not release permission, proof of
license compliance, signature authenticity, loader compatibility, or no secrets.
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import unicodedata
import zipfile
import zlib
from dataclasses import asdict, dataclass
from typing import BinaryIO

SHA = re.compile(r"[a-f0-9]{64}\Z")
COMMIT = re.compile(r"(?:[a-f0-9]{40}|[a-f0-9]{64})\Z")
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?\Z")
DEVICE = re.compile(r"(?:con|prn|aux|nul|clock\$|conin\$|conout\$|com[1-9¹²³]|lpt[1-9¹²³])\Z", re.I)
CHUNK = 64 * 1024


@dataclass(frozen=True)
class Limits:
    max_archive_bytes: int = 512 * 1024 * 1024
    max_central_directory_bytes: int = 8 * 1024 * 1024
    max_entries: int = 4096
    max_file_bytes: int = 128 * 1024 * 1024
    max_total_bytes: int = 512 * 1024 * 1024
    max_ratio: int = 200
    max_json_bytes: int = 1024 * 1024
    max_source_archive_bytes: int = 32 * 1024 * 1024
    max_scan_bytes: int = 256 * 1024


DEFAULT_POLICY = {
    "policyVersion": 1,
    "allowedProductIds": [],
    "allowedModuleIds": [],
    "acceptedObsTargets": [],
    "requiredRuntimeDlls": ["obs.dll", "obs-frontend-api.dll", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"],
    "allowedRuntimeLocations": ["host-obs-bin", "system32"],
    "requiredFiles": ["README-ja.txt", "LICENSE.txt", "THIRD-PARTY-NOTICES.txt", "SOURCE-CODE.txt", "CHANGELOG.txt"],
    "requiredLocales": ["ja-JP", "en-US", "zh-CN", "zh-TW", "ko-KR", "es-ES", "pt-BR", "fr-FR", "de-DE", "it-IT", "ru-RU", "id-ID", "tr-TR", "th-TH", "vi-VN", "pl-PL"],
    "requiredSourcePaths": ["BUILDING.md", "CMakeLists.txt"],
    "allowedExtensionsByRole": {
        "module": [".dll"], "data": [".ini", ".json", ".txt", ".png", ".svg"],
        "locale": [".ini"], "license": [".txt", ".md", ""],
        "readme": [".txt", ".md"], "notice": [".txt", ".md"],
        "source-info": [".txt", ".md"], "changelog": [".txt", ".md"], "source": [".zip"]
    },
    "secretRules": [
        {"id": "private-key-filename", "severity": "fail", "pathGlob": "*.pfx"},
        {"id": "private-key-filename", "severity": "fail", "pathGlob": "*.p12"},
        {"id": "private-key-filename", "severity": "fail", "pathGlob": "*.key"},
        {"id": "env-file-review", "severity": "warning", "pathGlob": "*.env*"},
        {"id": "private-key-marker", "severity": "fail", "contentPattern": "-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----"},
        {"id": "credential-word-review", "severity": "warning", "contentPattern": "(?i)(?:api[_-]?key|access[_-]?token|password)\\s*[:=]"}
    ]
}


class Invalid(Exception):
    def __init__(self, code: str, message: str):
        self.code, self.message = code, message
        super().__init__(message)


@dataclass(frozen=True)
class Issue:
    severity: str
    code: str
    message: str
    path: str | None = None
    scope: str = "integrity"


def path_key(raw: str, *, directory: bool = False) -> str:
    """Conservative NFC/casefold Windows key. Reject aliases, don't repair them."""
    if not isinstance(raw, str) or not raw or len(raw.encode("utf-16-le")) // 2 > 240:
        raise Invalid("PATH_UNSAFE", "Empty, non-string, or overlong path")
    if "\\" in raw or ":" in raw or raw.startswith("/"):
        raise Invalid("PATH_UNSAFE", "Absolute, drive, ADS, UNC, or noncanonical separator")
    if directory:
        if not raw.endswith("/"):
            raise Invalid("PATH_UNSAFE", "Directory lacks canonical trailing slash")
        raw = raw[:-1]
    for part in raw.split("/"):
        if (part in ("", ".", "..") or part[-1:] in (" ", ".") or
                any(ord(c) < 32 or ord(c) == 127 or c in '<>"|?*' for c in part) or
                len(part.encode("utf-16-le")) // 2 > 255):
            raise Invalid("PATH_UNSAFE", "Traversal, empty segment, illegal character, or Windows alias")
        if DEVICE.fullmatch(part.split(".", 1)[0].rstrip(" ")):
            raise Invalid("PATH_UNSAFE", "Windows reserved device name")
    return unicodedata.normalize("NFC", raw).casefold()


def strict_json(data: bytes):
    def pairs(items):
        obj = {}
        for key, value in items:
            if key in obj:
                raise Invalid("JSON_DUPLICATE_KEY", "Duplicate JSON object key")
            obj[key] = value
        return obj
    try:
        return json.loads(data.decode("utf-8-sig"), object_pairs_hook=pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite")))
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise Invalid("JSON_INVALID", "Invalid UTF-8 JSON or excessive nesting") from exc


def nonempty(value) -> bool:
    return isinstance(value, str) and bool(value.strip()) and not re.search(
        r"(?i)(?:unresolved|unknown|not-built|unverified|placeholder|^tbd$)", value)


def validate_policy(policy):
    if not isinstance(policy, dict) or type(policy.get("policyVersion")) is not int or policy.get("policyVersion") != 1:
        raise Invalid("POLICY_INVALID", "policyVersion must be 1")
    for name in ("allowedProductIds", "allowedModuleIds", "requiredRuntimeDlls", "allowedRuntimeLocations", "requiredFiles", "requiredLocales", "requiredSourcePaths"):
        if not isinstance(policy.get(name), list) or not all(nonempty(v) for v in policy[name]):
            raise Invalid("POLICY_INVALID", f"{name} must be a list of resolved strings (empty allowlists are permitted)")
    if not isinstance(policy.get("acceptedObsTargets"), list):
        raise Invalid("POLICY_INVALID", "acceptedObsTargets must be a list")
    for target in policy["acceptedObsTargets"]:
        if not isinstance(target, dict) or not all(nonempty(target.get(n)) for n in ("version", "channel", "sdkTag", "sdkCommit")):
            raise Invalid("POLICY_INVALID", "Malformed accepted OBS target")
    roles = policy.get("allowedExtensionsByRole")
    if not isinstance(roles, dict) or not roles:
        raise Invalid("POLICY_INVALID", "Role extension policy is required")
    for role, extensions in roles.items():
        if not nonempty(role) or not isinstance(extensions, list) or not all(isinstance(x, str) and (not x or re.fullmatch(r"\.[a-z0-9]+", x)) for x in extensions):
            raise Invalid("POLICY_INVALID", "Malformed role extension list")
    for p in policy["requiredFiles"] + policy["requiredSourcePaths"]:
        path_key(p)
    rules = policy.get("secretRules")
    if not isinstance(rules, list):
        raise Invalid("POLICY_INVALID", "secretRules must be a list")
    for rule in rules:
        if not isinstance(rule, dict) or not nonempty(rule.get("id")) or rule.get("severity") not in ("warning", "fail"):
            raise Invalid("POLICY_INVALID", "Secret rule needs id and warning/fail severity")
        if set(rule) - {"id", "severity", "pathGlob", "contentPattern"} or ("pathGlob" in rule) == ("contentPattern" in rule):
            raise Invalid("POLICY_INVALID", "Secret rule needs exactly one pathGlob/contentPattern")
        expr = rule.get("contentPattern", rule.get("pathGlob"))
        if not isinstance(expr, str) or not expr or len(expr) > 512:
            raise Invalid("POLICY_INVALID", "Invalid secret rule expression")
        if "contentPattern" in rule:
            try:
                re.compile(expr)
            except re.error as exc:
                raise Invalid("POLICY_INVALID", "Invalid content rule regex") from exc
    return policy


class Inventory:
    """Bounded, no-extraction view of one already-open ZIP."""
    def __init__(self, stream: BinaryIO, limits: Limits):
        self.stream, self.limits = stream, limits
        self.entries, self.directories, self.hashes, self.samples = {}, set(), {}, {}
        self.zip = None
        self._preflight()

    def _preflight(self):
        f, lim = self.stream, self.limits
        f.seek(0, 2)
        size = f.tell()
        if size > lim.max_archive_bytes:
            raise Invalid("ZIP_LIMIT", "Archive byte limit exceeded before ZIP parsing")
        # Reject ZIP64/multidisk and oversized central directories BEFORE ZipFile
        # allocates its per-entry objects. Bounded tail includes maximum comment.
        f.seek(max(0, size - 65557))
        tail = f.read(65557)
        at = tail.rfind(b"PK\x05\x06")
        if at < 0 or at + 22 > len(tail):
            raise Invalid("ZIP_INVALID", "Missing ZIP end-of-central-directory")
        _, disk, cd_disk, on_disk, count, cd_size, cd_offset, comment = struct.unpack_from("<4s4H2IH", tail, at)
        eocd_offset = max(0, size - 65557) + at
        if at + 22 + comment != len(tail):
            raise Invalid("ZIP_INVALID", "Ambiguous/trailing bytes after ZIP directory")
        if disk or cd_disk or on_disk != count or count == 65535 or cd_size == 0xffffffff or cd_offset == 0xffffffff:
            raise Invalid("ZIP_UNSUPPORTED", "ZIP64 and multidisk archives are intentionally unsupported")
        if (count > lim.max_entries or cd_size > lim.max_central_directory_bytes or
                cd_offset + cd_size != eocd_offset):
            raise Invalid("ZIP_LIMIT", "Entry/directory limit or unsupported prefixed archive")
        f.seek(0)
        self.zip = zipfile.ZipFile(f, "r")
        infos = self.zip.infolist()
        if len(infos) != count:
            raise Invalid("ZIP_INVALID", "Central directory entry count disagreement")
        if infos and min(info.header_offset for info in infos) != 0:
            raise Invalid("ZIP_UNSUPPORTED", "Prefixed/self-extracting archives are unsupported")
        f.seek(0)
        if infos and f.read(4) != b"PK\x03\x04":
            raise Invalid("ZIP_UNSUPPORTED", "ZIP must start with its first local file header")
        seen = set()
        total = 0
        for info in infos:
            # orig_filename retains a NUL which ZipInfo.filename silently truncates.
            raw = info.orig_filename
            is_dir = raw.endswith("/")
            key = path_key(raw, directory=is_dir)
            if key in seen:
                raise Invalid("PATH_DUPLICATE", "Duplicate NFC/casefold Windows path")
            seen.add(key)
            unix_type = stat.S_IFMT(info.external_attr >> 16)
            if unix_type not in (0, stat.S_IFDIR if is_dir else stat.S_IFREG) or info.external_attr & 0x400:
                raise Invalid("ZIP_LINK_OR_SPECIAL", "Symlink, special file, or Windows reparse attribute")
            if info.volume != 0 or info.flag_bits & 1 or info.extract_version > 20 or info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise Invalid("ZIP_UNSUPPORTED", "Encrypted or unsupported compression entry")
            if info.file_size > lim.max_file_bytes or info.file_size < 0 or info.compress_size < 0:
                raise Invalid("ZIP_LIMIT", "Per-file size limit exceeded")
            if info.file_size > max(1, info.compress_size) * lim.max_ratio:
                raise Invalid("ZIP_LIMIT", "Compression ratio limit exceeded")
            total += info.file_size
            if total > lim.max_total_bytes:
                raise Invalid("ZIP_LIMIT", "Aggregate uncompressed limit exceeded")
            if is_dir:
                if info.file_size:
                    raise Invalid("ZIP_INVALID", "Directory carries file data")
                self.directories.add(key)
            else:
                self.entries[key] = info
        for key in seen:
            parts = key.split("/")
            if any("/".join(parts[:i]) in self.entries for i in range(1, len(parts))):
                raise Invalid("PATH_COLLISION", "A file is an ancestor of another archive entry")

    def close(self):
        if self.zip:
            self.zip.close()

    def read_small(self, key: str, limit: int) -> bytes:
        info = self.entries.get(key)
        if info is None:
            raise Invalid("FILE_MISSING", "Required archive entry is missing")
        if info.file_size > limit:
            raise Invalid("ZIP_LIMIT", "Structured input exceeds its read limit")
        with self.zip.open(info) as f:
            data = f.read(limit + 1)
        if len(data) != info.file_size or len(data) > limit:
            raise Invalid("ZIP_SIZE", "Actual entry size disagrees with bounded metadata")
        return data

    def hash_all(self):
        total = 0
        for key, info in self.entries.items():
            h, count, sample = hashlib.sha256(), 0, bytearray()
            with self.zip.open(info) as f:
                while block := f.read(CHUNK):
                    count += len(block)
                    total += len(block)
                    if count > self.limits.max_file_bytes or total > self.limits.max_total_bytes:
                        raise Invalid("ZIP_LIMIT", "Stream exceeded decompressed byte budget")
                    h.update(block)
                    sample.extend(block[:max(0, self.limits.max_scan_bytes - len(sample))])
            if count != info.file_size:
                raise Invalid("ZIP_SIZE", "Actual decompressed size mismatch")
            self.hashes[key] = h.hexdigest()
            self.samples[key] = bytes(sample)


class Validator:
    def __init__(self, policy=None, limits=None):
        self.policy = validate_policy(policy if policy is not None else DEFAULT_POLICY)
        self.limits = limits or Limits()
        self.issues: list[Issue] = []
        self.checked_manifests = []

    def add(self, severity, code, message, path=None, scope="integrity"):
        self.issues.append(Issue(severity, code, message, path, scope))

    def gate(self, ok, code, message, path=None):
        if not ok:
            self.add("fail", code, message, path, "release")
        return bool(ok)

    def manifest(self, inv, path):
        key = path_key(path)
        data = strict_json(inv.read_small(key, self.limits.max_json_bytes))
        self.checked_manifests.append(path)
        return key, data

    def compare(self, inv, rows, base, manifest_key, path_field, size_field, require_size=True, exact_scope=None):
        if not isinstance(rows, list) or not rows:
            self.add("fail", "MANIFEST_FILES", "Nonempty files array is required", manifest_key)
            return {}
        listed = {}
        for row in rows:
            if not isinstance(row, dict):
                self.add("fail", "MANIFEST_FILE", "File row must be an object", manifest_key)
                continue
            try:
                relative = row.get(path_field)
                relative_key = path_key(relative)
                key = base + relative_key
                if key == manifest_key:
                    raise Invalid("MANIFEST_SELF_REFERENCE", "Manifest must exclude itself from its own file list")
                if key in listed:
                    raise Invalid("MANIFEST_DUPLICATE", "Duplicate normalized manifest path")
                listed[key] = row
                info = inv.entries.get(key)
                if info is None:
                    raise Invalid("FILE_MISSING", "Listed file is missing")
                if not isinstance(row.get("sha256"), str) or not SHA.fullmatch(row["sha256"]):
                    raise Invalid("HASH_FORMAT", "sha256 must be 64 lowercase hex digits")
                if row["sha256"] != inv.hashes[key]:
                    raise Invalid("HASH_MISMATCH", "Declared sha256 differs from streamed file bytes")
                size = row.get(size_field)
                if require_size or size is not None:
                    if type(size) is not int or size < 0:
                        raise Invalid("SIZE_FORMAT", "Size must be a nonnegative integer")
                    if size != info.file_size:
                        raise Invalid("SIZE_MISMATCH", "Declared size differs from archive bytes")
            except (Invalid, UnicodeError) as exc:
                self.add("fail", getattr(exc, "code", "PATH_UNSAFE"), getattr(exc, "message", "Invalid Unicode path"), row.get(path_field))
        if exact_scope is not None:
            actual = {k for k in inv.entries if k.startswith(exact_scope)} - {manifest_key}
            for key in sorted(actual - listed.keys()):
                self.add("fail", "FILE_UNLISTED", "Archive file has no manifest row", inv.entries[key].orig_filename)
        return listed

    def scan(self, inv, prefix=""):
        for key, info in inv.entries.items():
            for rule in self.policy["secretRules"]:
                matched = False
                if "pathGlob" in rule:
                    matched = fnmatch.fnmatchcase(key, rule["pathGlob"].casefold())
                else:
                    text = inv.samples[key].decode("utf-8", "replace")
                    matched = bool(re.search(rule["contentPattern"], text))
                if matched:
                    # Only rule ID/path are reported, never matched credentials.
                    self.add(rule["severity"], "SECRET_RULE_MATCH", "Heuristic rule matched: " + rule["id"], prefix + info.orig_filename)

    def release(self, inv, key, doc):
        if not isinstance(doc, dict):
            self.add("fail", "MANIFEST_SCHEMA", "Release manifest must be an object", key)
            return
        if not self.gate(type(doc.get("schemaVersion")) is int and doc["schemaVersion"] == 1, "SCHEMA_VERSION", "schemaVersion must be integer 1"):
            return
        files = self.compare(inv, doc.get("files"), "", key, "relativePath", "size", exact_scope="")
        p = self.policy
        self.gate(doc.get("draftOnly") is False, "DRAFT_BLOCKED", "draftOnly must be explicitly false")
        self.gate(doc.get("artifactKind") == "product", "ARTIFACT_KIND", "Only product artifacts can satisfy the release gate")
        self.gate(doc.get("architecture") == "amd64", "ARCHITECTURE", "Only amd64 is supported")
        self.gate(doc.get("layoutVersion") == "obs33-flat-v1", "LAYOUT", "Expected obs33-flat-v1")
        self.gate(isinstance(doc.get("supportedModes"), list) and all(isinstance(x, str) for x in doc["supportedModes"]) and set(doc["supportedModes"]) == {"normal", "portable"} and len(doc["supportedModes"]) == 2, "MODES", "Both normal and portable modes must be declared exactly once")
        for field in ("runtimeAuditComplete", "licenseAuditComplete", "releaseAllowed"):
            self.gate(doc.get(field) is True, "RELEASE_GATE", field + " is not true")
        signature = doc.get("signature")
        self.gate(isinstance(signature, dict) and all(signature.get(n) in ("verified", "unsigned") for n in ("installer", "module")), "SIGNATURE_STATUS", "Built installer/module must declare verified or unsigned signing status; this tool does not verify signatures")
        product, module = doc.get("productId"), doc.get("moduleId")
        self.gate(isinstance(product, str) and product in p["allowedProductIds"], "PRODUCT_ID", "Product ID is not in the explicit policy allowlist")
        module_ok = self.gate(isinstance(module, str) and bool(re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", module)) and module in p["allowedModuleIds"], "MODULE_ID", "Module ID is unresolved or outside the explicit allowlist")
        self.gate(isinstance(doc.get("productVersion"), str) and bool(VERSION.fullmatch(doc["productVersion"])), "PRODUCT_VERSION", "Product version must be exact semver-like x.y.z[-prerelease]")
        commit = doc.get("releaseCommit")
        self.gate(isinstance(commit, str) and bool(COMMIT.fullmatch(commit)), "RELEASE_COMMIT", "Resolved release commit is required")
        for field in ("obsSdkTag", "qtBuildVersion", "compilerVersion"):
            self.gate(nonempty(doc.get(field)), "BUILD_METADATA", field + " is unresolved")
        self.gate(isinstance(doc.get("obsSdkCommit"), str) and bool(COMMIT.fullmatch(doc["obsSdkCommit"])), "OBS_SDK_COMMIT", "Exact OBS SDK commit is required")
        self.obs_targets(doc)
        self.runtime(doc)
        license_ids = self.licenses(inv, doc, files)
        modules = []
        for file_key, row in files.items():
            role, ext = row.get("role"), PurePosixPath(file_key).suffix.casefold()
            self.gate(isinstance(role, str) and role in p["allowedExtensionsByRole"] and ext in p["allowedExtensionsByRole"].get(role, []), "EXTENSION_ROLE", "File extension/role is outside the explicit allowlist", file_key)
            self.gate(nonempty(row.get("componentLicenseId")) and row.get("componentLicenseId") in license_ids, "FILE_LICENSE", "File must reference a declared component license", file_key)
            if file_key.startswith("data/locale/"):
                self.gate(role == "locale", "LOCALE_ROLE", "Locale entries must use the locale role", file_key)
                self.gate(file_key in {"data/locale/" + locale.casefold() + ".ini" for locale in p["requiredLocales"]}, "LOCALE_ALLOWLIST", "Unexpected locale filename or nested locale path", file_key)
            name = PurePosixPath(file_key).name
            forbidden = (re.match(r"(?i)(?:qt[56].*|libobs.*|obs(?:-frontend-api)?|vcruntime.*|msvcp.*|ucrtbase|msvcr.*)\.dll\Z", name) or
                         re.search(r"(?i)(?:vc_redist.*\.exe|(?:^|[/_-])(?:poc|fixture|debug)(?:[/_.-]|$))", file_key))
            self.gate(not forbidden, "FORBIDDEN_RUNTIME_OR_FIXTURE", "Initial product set excludes bundled host runtimes and PoC/debug fixtures", file_key)
            if role == "module":
                modules.append(file_key)
            if role in ("data", "locale"):
                self.gate(file_key.startswith("data/"), "DATA_LAYOUT", "Data/locale must remain under data/", file_key)
        self.gate(module_ok and modules == [str(module) + ".dll"] and modules[0] in inv.entries and inv.entries[modules[0]].file_size > 0, "MODULE_LAYOUT", "Exactly one module DLL at <moduleId>.dll is required")
        for path in p["requiredFiles"] + ["data/locale/" + locale + ".ini" for locale in p["requiredLocales"]]:
            k = path_key(path)
            self.gate(k in files and k in inv.entries and inv.entries[k].file_size > 0, "REQUIRED_FILE", "Nonempty required distribution file is missing", path)
        self.source(inv, doc, files)
        self.add("info", "AUDIT_ASSERTIONS_ONLY", "True audit flags, metadata and hashes are declarations; this tool does not authenticate the publisher, inspect PE imports, prove source-to-binary correspondence, or approve publication", scope="release")

    def obs_targets(self, doc):
        rows = doc.get("acceptedObsVersions")
        if not self.gate(isinstance(rows, list) and bool(rows), "OBS_TARGETS", "Nonempty exact, QA-tested OBS target list is required"):
            return
        seen = set()
        for row in rows:
            if not self.gate(isinstance(row, dict), "OBS_TARGET", "OBS target must be an object"):
                continue
            version, channel = row.get("version"), row.get("channel")
            valid_version = isinstance(version, str) and bool(VERSION.fullmatch(version))
            is_rc = valid_version and "-" in version
            self.gate(valid_version and int(version.split(".")[0]) >= 33, "OBS_VERSION", "Product OBS target must be exact OBS 33+; OBS32 test artifacts cannot pass")
            self.gate(channel in ("stable", "rc") and (channel == "rc") == bool(is_rc), "OBS_CHANNEL", "RC/stable channel must match exact version")
            self.gate(nonempty(row.get("qaEvidenceId")), "OBS_QA", "Each target needs a QA evidence ID")
            self.gate(any(all(row.get(n) == target.get(n) for n in ("version", "channel")) and doc.get("obsSdkTag") == target.get("sdkTag") and doc.get("obsSdkCommit") == target.get("sdkCommit") for target in self.policy["acceptedObsTargets"]), "OBS_ALLOWLIST", "OBS version/channel/SDK tuple is not explicitly approved in policy")
            pair = (str(version), str(channel))
            self.gate(pair not in seen, "OBS_DUPLICATE", "Duplicate OBS target")
            seen.add(pair)
            self.gate((channel == "rc" and doc.get("channel") == "internal-rc") or (channel == "stable" and doc.get("channel") in ("stable", "beta")), "RELEASE_CHANNEL", "RC targets require internal-rc; stable targets require stable/beta package channel")

    def runtime(self, doc):
        rows = doc.get("runtimeRequirements")
        if not self.gate(isinstance(rows, list) and bool(rows), "RUNTIME_REQUIREMENTS", "Nonempty runtime requirement list is required"):
            return
        seen = set()
        for row in rows:
            if not self.gate(isinstance(row, dict), "RUNTIME_SCHEMA", "Runtime row must be an object"):
                continue
            name = row.get("dllName")
            valid = isinstance(name, str) and bool(re.fullmatch(r"[A-Za-z0-9_.-]+\.dll", name))
            self.gate(valid, "RUNTIME_NAME", "Runtime DLL name must be a basename")
            namekey = name.casefold() if valid else ""
            self.gate(namekey not in seen, "RUNTIME_DUPLICATE", "Duplicate runtime DLL")
            seen.add(namekey)
            for field in ("purpose", "inspectionMethod", "testedMinimumVersion"):
                self.gate(nonempty(row.get(field)), "RUNTIME_UNRESOLVED", field + " is unresolved")
            locations = row.get("allowedResolutionLocations")
            self.gate(isinstance(locations, list) and bool(locations) and all(isinstance(x, str) and x in self.policy["allowedRuntimeLocations"] for x in locations), "RUNTIME_LOCATION", "Runtime resolution must use policy-approved host/system locations")
            self.gate(not bool(re.search(r"(?i)(?:(?:qt[56].*|vcruntime[0-9_]*|msvcp[0-9_]*|msvcr[0-9_]*)d|ucrtbased)\.dll\Z", namekey)), "DEBUG_RUNTIME", "Debug runtimes are forbidden")
        for name in self.policy["requiredRuntimeDlls"]:
            self.gate(name.casefold() in seen, "RUNTIME_MISSING", "Required runtime dependency is missing: " + name)

    def licenses(self, inv, doc, files):
        rows, ids = doc.get("licenses"), set()
        if not self.gate(isinstance(rows, list) and bool(rows), "LICENSES", "Nonempty component license inventory is required"):
            return ids
        for row in rows:
            if not self.gate(isinstance(row, dict), "LICENSE_SCHEMA", "License entry must be an object"):
                continue
            identifier = row.get("id")
            valid = nonempty(identifier)
            self.gate(valid and identifier not in ids, "LICENSE_ID", "Unique component license id is required")
            if valid:
                ids.add(identifier)
            for field in ("component", "version", "commit", "licenseExpression", "sourceOrigin"):
                self.gate(nonempty(row.get(field)), "LICENSE_UNRESOLVED", "License " + field + " is unresolved")
            self.gate(type(row.get("distributed")) is bool, "LICENSE_DISTRIBUTION", "Each license must state whether its component is distributed")
            for field in ("noticePath", "licensePath"):
                try:
                    k = path_key(row.get(field))
                    self.gate(k in files and k in inv.entries and inv.entries[k].file_size > 0, "LICENSE_FILE", "License/notice path must reference a nonempty manifest-listed file", row.get(field))
                except (Invalid, UnicodeError):
                    self.gate(False, "LICENSE_PATH", field + " must be a safe relative package path")
        return ids

    def source(self, inv, doc, files):
        source = doc.get("sourceArchive")
        if not self.gate(isinstance(source, dict), "SOURCE_ARCHIVE", "sourceArchive object is required"):
            return
        self.gate(source.get("releaseCommit") == doc.get("releaseCommit") and isinstance(source.get("releaseCommit"), str) and bool(COMMIT.fullmatch(source["releaseCommit"])), "SOURCE_COMMIT", "Source archive release commit must match release manifest")
        self.gate(nonempty(source.get("availability")), "SOURCE_AVAILABILITY", "Source availability explanation is required (network retrieval is not checked)")
        try:
            key = path_key(source.get("filename"))
        except (Invalid, UnicodeError):
            self.gate(False, "SOURCE_FILENAME", "Safe source archive filename is required")
            return
        present = key in files and key in inv.entries
        self.gate(present, "SOURCE_MISSING", "Source archive must be included in the same validated package", source.get("filename"))
        self.gate(PurePosixPath(key).suffix == ".zip", "SOURCE_FORMAT", "This validator supports bundled ZIP source only")
        if not present:
            return
        self.gate(files[key].get("role") == "source", "SOURCE_ROLE", "Source archive must use the source role", key)
        self.gate(isinstance(source.get("sha256"), str) and bool(SHA.fullmatch(source["sha256"])) and source["sha256"] == inv.hashes[key], "SOURCE_HASH", "Source archive hash must match its streamed bytes", key)
        try:
            data = inv.read_small(key, self.limits.max_source_archive_bytes)
            nested = Inventory(io.BytesIO(data), self.limits)
            try:
                nested.hash_all()
                self.scan(nested, prefix=key + "!/")
                for path in self.policy["requiredSourcePaths"]:
                    k = path_key(path)
                    self.gate(k in nested.entries and nested.entries[k].file_size > 0, "SOURCE_REQUIRED_FILE", "Bundled source is missing required editable build material", key + "!/" + path)
                self.gate(any(k.startswith("src/") and k.endswith((".cpp", ".c", ".h", ".hpp")) and nested.entries[k].file_size > 0 for k in nested.entries), "SOURCE_EDITABLE", "Bundled source needs editable src/ C/C++ material")
                self.gate(any(PurePosixPath(k).name in ("license", "license.txt", "copying") and nested.entries[k].file_size > 0 for k in nested.entries), "SOURCE_LICENSE", "Bundled source needs its license/copyright file")
            finally:
                nested.close()
        except (Invalid, zipfile.BadZipFile, RuntimeError, NotImplementedError, OSError, zlib.error) as exc:
            self.add("fail", getattr(exc, "code", "SOURCE_ZIP_INVALID"), getattr(exc, "message", "Source archive is invalid or corrupt"), key)

    def export(self, inv, key, doc, archive_manifest=None):
        self.gate(False, "SOURCE_ONLY_NOT_RELEASE", "Clean source/review export is not a built release package; product, installer, OBS/runtime/license/source distribution gates remain incomplete")
        if not isinstance(doc, dict):
            self.add("fail", "MANIFEST_SCHEMA", "Export manifest must be an object", key)
            return
        base = key.rsplit("/", 1)[0] + "/" if "/" in key else ""
        listed = self.compare(inv, doc.get("files"), base, key, "path", "size", require_size=False, exact_scope=base)
        if type(doc.get("candidateFileCountIncludingManifest")) is not int or doc["candidateFileCountIncludingManifest"] != len(listed) + 1:
            self.add("fail", "EXPORT_COUNT", "candidateFileCountIncludingManifest disagrees with inventory")
        if doc.get("historyIncluded") is not False:
            self.add("fail", "EXPORT_HISTORY", "Clean export must explicitly declare historyIncluded=false")
        if not isinstance(doc.get("sourceCommit"), str) or not COMMIT.fullmatch(doc["sourceCommit"]):
            self.add("fail", "EXPORT_COMMIT", "Exact source commit is required")
        if archive_manifest:
            outer_key, outer = self.manifest(inv, archive_manifest)
            self.compare(inv, outer, "", outer_key, "path", "bytes", exact_scope="")
        else:
            self.add("warning", "OUTER_MANIFEST_NOT_CHECKED", "Export schema omits file sizes; supply outer archive manifest to verify size and full archive coverage")
        self.add("info", "EXPORT_SCHEMA", "Export file hashes verify exact bytes; commit ancestry, omitted history, source authorship and build provenance are not proven")

    def validate(self, archive, manifest="package-manifest.json", profile="release", archive_manifest=None, expected_sha256=None):
        self.issues, self.checked_manifests = [], []
        archive_hash = None
        inv = None
        fd = None
        try:
            path = Path(archive)
            if not stat.S_ISREG(path.lstat().st_mode):
                raise Invalid("INPUT_NOT_REGULAR", "Archive input must be a regular file, not a symlink/device")
            fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            with os.fdopen(fd, "rb") as f:
                fd = None
                initial = os.fstat(f.fileno())
                if not stat.S_ISREG(initial.st_mode):
                    raise Invalid("INPUT_NOT_REGULAR", "Archive changed to a nonregular file")
                if initial.st_size > self.limits.max_archive_bytes:
                    raise Invalid("ZIP_LIMIT", "Archive byte limit exceeded before parsing")
                h, archive_bytes_read = hashlib.sha256(), 0
                for block in iter(lambda: f.read(CHUNK), b""):
                    archive_bytes_read += len(block)
                    if archive_bytes_read > self.limits.max_archive_bytes:
                        raise Invalid("ZIP_LIMIT", "Archive grew beyond its byte limit during hashing")
                    h.update(block)
                archive_hash = h.hexdigest()
                if expected_sha256 is not None and (not isinstance(expected_sha256, str) or not SHA.fullmatch(expected_sha256) or archive_hash != expected_sha256):
                    raise Invalid("ARCHIVE_HASH", "Archive does not match the expected SHA256")
                inv = Inventory(f, self.limits)
                try:
                    inv.hash_all()
                    self.scan(inv)
                    key, doc = self.manifest(inv, manifest)
                    if profile == "release":
                        self.release(inv, key, doc)
                    elif profile == "export":
                        self.export(inv, key, doc, archive_manifest)
                    else:
                        raise Invalid("PROFILE_INVALID", "Unknown validation profile")
                    final = os.fstat(f.fileno())
                    if (initial.st_dev, initial.st_ino, initial.st_size, initial.st_mtime_ns, initial.st_ctime_ns) != (final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns, final.st_ctime_ns):
                        raise Invalid("INPUT_CHANGED", "Archive input changed during validation")
                finally:
                    inv.close()
        except (Invalid, OSError, zipfile.BadZipFile, RuntimeError, NotImplementedError, EOFError, UnicodeError, ValueError, TypeError, struct.error, zlib.error) as exc:
            self.add("fail", getattr(exc, "code", "INPUT_INVALID"), getattr(exc, "message", "Archive could not be safely read or is corrupt"))
        finally:
            if fd is not None:
                os.close(fd)
        integrity_ok = not any(i.severity == "fail" and i.scope == "integrity" for i in self.issues)
        ready = not any(i.severity == "fail" for i in self.issues)
        return {
            "validatorVersion": "1.0.0", "profile": profile,
            "status": "CHECKS_PASSED" if ready else "NOT_READY" if integrity_ok else "INVALID",
            "integrityPassed": integrity_ok, "releaseChecksPassed": ready,
            "publicationAuthorized": False, "archiveSha256": archive_hash,
            "checkedFileCount": len(inv.hashes) if inv else 0,
            "checkedManifests": self.checked_manifests,
            "limits": asdict(self.limits), "issues": [asdict(i) for i in self.issues],
            "limitations": ["No installation, extraction, execution, deletion, network access, or original-file changes", "Secret rules scan only a bounded UTF-8-decoded prefix per entry, plus names; no absence-of-secrets guarantee", "Source ZIP inspected one level deep; embedded archives and binary content need separate review", "Policy and manifests are local assertions, not signatures, legal conclusions, or distribution approval"]
        }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", help="Existing candidate ZIP; read only")
    parser.add_argument("--manifest", default="package-manifest.json", help="Manifest path inside ZIP")
    parser.add_argument("--profile", choices=("release", "export"), default="release")
    parser.add_argument("--archive-manifest", help="Outer archive manifest path inside ZIP (export profile)")
    parser.add_argument("--expected-sha256", help="Expected archive hash from an independently trusted record")
    parser.add_argument("--policy", help="Local JSON policy (not fetched, modified, or treated as user approval)")
    args = parser.parse_args(argv)
    try:
        policy = DEFAULT_POLICY
        if args.policy:
            with open(args.policy, "rb") as f:
                data = f.read(Limits().max_json_bytes + 1)
            if len(data) > Limits().max_json_bytes:
                raise Invalid("POLICY_LIMIT", "Policy exceeds byte limit")
            policy = strict_json(data)
        result = Validator(policy).validate(args.archive, args.manifest, args.profile, args.archive_manifest, args.expected_sha256)
    except (Invalid, OSError) as exc:
        result = {"status": "INVALID", "integrityPassed": False, "releaseChecksPassed": False, "publicationAuthorized": False,
                  "issues": [asdict(Issue("fail", getattr(exc, "code", "POLICY_INVALID"), getattr(exc, "message", "Policy could not be read")))]}
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0 if result["status"] == "CHECKS_PASSED" else 1 if result["status"] == "NOT_READY" else 2


if __name__ == "__main__":
    sys.exit(main())
