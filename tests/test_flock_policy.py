from pathlib import Path
import re


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
ALLOWED_FLOCK_SOURCE = Path("src/session/session_lifecycle_lease.h")


def test_product_flock_is_confined_to_per_session_lifecycle_lease() -> None:
    """Global registry/query paths must never regain a process-wide flock."""
    offenders: list[str] = []
    for path in sorted((REPOSITORY_ROOT / "src").rglob("*")):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp", ".py"}:
            continue
        relative = path.relative_to(REPOSITORY_ROOT)
        if relative == ALLOWED_FLOCK_SOURCE:
            continue
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if re.search(r"\bflock\s*\(", line):
                offenders.append(f"{relative}:{line_number}")

    assert offenders == [], (
        "flock may only be used by the stable per-session lifecycle lease: "
        + ", ".join(offenders)
    )
