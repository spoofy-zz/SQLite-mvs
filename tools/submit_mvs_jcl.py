#!/usr/bin/env python3
"""Submit local JCL through the same MBT endpoint used for deployment."""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "mbt" / "scripts"))

from mbt.config import MbtConfig
from mbt.mvsmf import MvsMFClient, MvsMFError


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("jcl", type=Path)
    parser.add_argument("--project", default="project.toml")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--spool", type=Path)
    args = parser.parse_args()

    config = MbtConfig(project_path=args.project)
    client = MvsMFClient(host=config.mvs_host, port=config.mvs_port,
                         user=config.mvs_user, password=config.mvs_pass)
    try:
        result = client.submit_jcl(args.jcl.read_text(), timeout=args.timeout)
    except (OSError, MvsMFError) as error:
        print("submit failed: %s" % error, file=sys.stderr)
        return 4
    spool = result.spool or ""
    if args.spool:
        args.spool.parent.mkdir(parents=True, exist_ok=True)
        args.spool.write_text(spool)
    print("%s %s status=%s rc=%s" %
          (result.jobname, result.jobid, result.status, result.rc))
    if spool:
        print(spool)
    return 0 if result.status == "CC" and result.rc <= 4 else 8


if __name__ == "__main__":
    sys.exit(main())
