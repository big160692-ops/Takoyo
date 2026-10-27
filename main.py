"""Takoyo AI Agent - entry point.

Run the desktop app:         python main.py
Run the app plus its API:    python main.py --api
Run only the HTTP API:       python main.py --api-only
Set up the local AI:         python main.py --setup-local
"""

import argparse


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="takoyo", description="Takoyo AI Agent")
    parser.add_argument(
        "--api", action="store_true", help="also start the built-in HTTP API with the GUI"
    )
    parser.add_argument(
        "--api-only", action="store_true", help="run only the HTTP API server (no GUI)"
    )
    parser.add_argument("--host", default=None, help="API bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=None, help="API port (default: 8765)")
    parser.add_argument(
        "--setup-local",
        nargs="?",
        const="",
        metavar="MODEL",
        help="download the built-in AI engine + model, make it the default, then exit",
    )
    return parser


def main(argv=None) -> None:
    args = build_parser().parse_args(argv)
    if args.setup_local is not None:
        from takoyo_agent.local_ai import setup_local_cli

        raise SystemExit(setup_local_cli(args.setup_local or None))
    if args.api_only:
        from takoyo_agent.api import run_api_server

        run_api_server(host=args.host, port=args.port)
        return
    from takoyo_agent.gui import main as gui_main

    gui_main(start_api=args.api, api_host=args.host, api_port=args.port)


if __name__ == "__main__":
    main()
