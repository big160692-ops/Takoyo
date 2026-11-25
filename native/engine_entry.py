"""API-only entry point used by the single-file native build."""

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(prog="takoyo-engine")
    parser.add_argument("--api-only", action="store_true")
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    args, _unknown = parser.parse_known_args()

    from takoyo_agent.api import run_api_server

    run_api_server(host=args.host, port=args.port)


if __name__ == "__main__":
    main()
