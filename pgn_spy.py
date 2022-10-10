from xml.etree import ElementTree as ET
import pandas as pd
import chess.pgn
import subprocess
from typing import List
import re


def get_list_of_games(pgn_path: str) -> List[chess.pgn.Game]:
    pgn = open(pgn_path)
    games = []
    while True:
        game = chess.pgn.read_game(pgn)
        if game is None:
            break
        games.append(game)
    pgn.close()
    return games


def convert_game_to_uci(game: chess.pgn.Game):
    uci_headers = []
    for header, val in game.headers.items():
        uci_headers.append(f'[{header} "{val}"]')
    uci_str = "\n".join(uci_headers)
    uci_str += "\n\n"
    current_move = game.next()
    while current_move:
        uci_str += f"{current_move.uci()} "
        current_move = current_move.next()
    uci_str += game.headers.get("Result", "")
    uci_str += "\n"
    return uci_str


class PGNSpy:
    analyser_binary_path = "/root/app/uci-analyser/analyse"
    engine_binary_path = "/stockfish_15_linux_x64/stockfish_15_src/src/stockfish"

    def __init__(self, pgn_file: str, player: str, book_depth: int, engine_depth: int, undecided_thres: int, lost_thres: int):
        self.pgn_file = pgn_file
        self.player = player
        self.book_depth = book_depth
        self.engine_depth = engine_depth
        self.undecided_thres = undecided_thres
        self.lost_thres = lost_thres

    def analyse_game(self, single_game: chess.pgn.Game, game_nr: int, coloroption: str) -> str:
        uci_str = convert_game_to_uci(single_game)
        lag_path = f"long_algebraic_game_{game_nr}.pgn"
        with open(lag_path, "w") as lag_file:
            lag_file.write(uci_str)
        num_vars = 3
        params = [self.analyser_binary_path,
                  "--engine", self.engine_binary_path,
                  "--searchdepth", str(self.engine_depth),
                  "--bookdepth", str(self.book_depth),
                  "--variations", str(num_vars),
                  coloroption,
                  lag_path
                  ]
        p = subprocess.run(params, shell=False, capture_output=True)
        out_file = p.stdout.decode()
        out_path = f"{lag_path[:-4]}_results.xml"
        with open(out_path, "w") as f:
            f.write(out_file)
        return out_path

    def result_xml_to_df(self, path_to_result_xml: str) -> pd.DataFrame:
        etree = ET.parse(path_to_result_xml)
        gamelist = etree.getroot()
        game = gamelist.find("game")
        analysis = game.find("analysis")
        data = []
        for move in analysis.findall("move"):
            played_move = move.find("played").text
            options = move.findall("evaluation")
            eval_options = []
            played_eval = None
            for option in options:
                try:
                    option_eval = float(option.attrib["value"])
                except ValueError as e:
                    if re.match("mate [0-9]{1,2}", option.attrib["value"]):
                        option_eval = 1000  # rate as +10
                    elif re.match("mate -[0-9]{1,2}", option.attrib["value"]):
                        option_eval = -1000  # rate as +10
                    else:
                        raise e
                eval_options.append(option_eval)
                if option.attrib["move"] == played_move:
                    played_eval = option_eval
            if played_eval is None:
                raise ValueError("No eval for played option")
            data.append([played_move, played_eval, *eval_options])

        n_options = max([len(mv) for mv in data])
        for idx in range(len(data)):
            if len(data[idx]) < n_options:
                data[idx] = data[idx] + [None for _ in range(n_options - len(data[idx]))]
        columns = ["move", "eval"] + [f"best_{i + 1}" for i in range(n_options - 2)]
        df = pd.DataFrame(data, columns=columns)
        return df

    def assign_position_type(self, pos_eval: float):
        if abs(pos_eval) <= self.undecided_thres:
            return "undecided"
        elif self.undecided_thres < pos_eval < self.lost_thres:
            return "winning"
        elif -self.undecided_thres > pos_eval > -self.lost_thres:
            return "losing"
        else:
            return "post win or lose"

    def analyze_player(self):
        all_games = get_list_of_games(self.pgn_file)
        all_results = pd.DataFrame()
        orig_ply_count = 0
        for idx, single_game in enumerate(all_games):
            if single_game.headers["White"] == self.player:
                color_option = "--whiteonly"
            elif single_game.headers["Black"] == self.player:
                color_option = "--blackonly"
            else:
                continue
            result_xml = self.analyse_game(single_game, idx, color_option)
            orig_ply_count += int(single_game.headers.get("PlyCount", "0") / 2)
            result_df = self.result_xml_to_df(result_xml)
            all_results = pd.concat([all_results, result_df])
        all_results = all_results.reset_index(drop=True)
        print(f"after dropping opening moves: {all_results.shape[0]} / {orig_ply_count}")
        all_results["centipawn_loss"] = all_results["best_1"] - all_results["eval"]
        n_vars = int([c for c in all_results.columns if c.startswith("best_")][-1].split("_")[-1])
        for i in range(1, n_vars):
            all_results[f"diff_{i}_{i+1}"] = all_results[f"best_{i}"] - all_results[f"best_{i+1}"]
            all_results[f"played_{i}_best"] = all_results["eval"] == all_results[f"best_{i}"]
        all_results["pos_type"] = all_results["best_1"].apply(self.assign_position_type)
        all_results["dummy_col"] = 1
        print(all_results)
        grouped_results = all_results\
            .groupby("pos_type", as_index=False)\
            .aggregate({"dummy_col": "sum", **{f"played_{i}_best": "sum" for i in range(1, n_vars)}, "centipawn_loss": "mean"})
        grouped_results.columns = ["Position Type", "# Positions", *[f"# {i} best" for i in range(1, n_vars)], "Avg. Centipawn Loss"]
        print(grouped_results)
        return grouped_results
