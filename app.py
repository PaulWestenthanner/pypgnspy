import base64
from dash import Dash, dcc, html, Input, Output, State, dash_table
from pgn_spy import PGNSpy, get_list_of_games
app = Dash(__name__)

app.layout = html.Div(children=[
    html.Title("PGN Spy"),
    html.H1(children='PGN Spy'),
    html.H2("Step 1: Upload your .pgn file"),
    dcc.Upload(
        id='upload-pgn',
        children=html.Div([
            'Drag and Drop or ',
            html.A('Select Files')
        ]),
        style={
            'width': '100%',
            'height': '60px',
            'lineHeight': '60px',
            'borderWidth': '1px',
            'borderStyle': 'dashed',
            'borderRadius': '5px',
            'textAlign': 'center',
            'margin': '10px'
        }
    ),
    html.H2("Step 2: Select Player:"),
    dcc.Dropdown(id="dd_player"),
    html.H2("Step 3: Select parameters for analysis"),
    "Select engine depth:",
    dcc.Input(id="engine_depth", type="number", value=15, step=1),
    "Select book depth (in plys, excluded from analysis):",
    dcc.Input(id="book_depth", type="number", value=20, step=1),
    "Select undecided position threshold (in centipawns):",
    dcc.Input(id="undecided_thres", type="number", value=200, min=0, step=10),
    "Select lost position threshold (in centipawns):",
    dcc.Input(id="lost_thres", type="number", value=500, min=0, step=10),
    html.Br(),
    html.Button("Analyse!", id="run_button", n_clicks=0),
    html.Div(id="output_container", children=[])
])


@app.callback(Output('dd_player', 'options'),
              Output('upload-pgn', 'children'),
              Input('upload-pgn', 'contents'),
              State('upload-pgn', 'filename'),
              )
def update_output(content, filename):
    if not content:
        return [{"label": "upload pgn first", "value": "err"}], html.Div(['Drag and Drop or ', html.A('Select Files')])
    content_type, content_string = content.split(',')
    decoded = base64.b64decode(content_string)
    tmp_filename = "tmp_" + filename
    with open(tmp_filename, "w") as f:
        f.write(decoded.decode("utf-8"))
    list_of_games = get_list_of_games(tmp_filename)
    set_of_players = set()
    for game in list_of_games:
        white = game.headers["White"]
        black = game.headers["Black"]
        if white not in set_of_players:
            set_of_players.add(white)
        if black not in set_of_players:
            set_of_players.add(black)
    return [{"label": pl, "value": pl} for pl in set_of_players], f"Uploaded {filename}"


@app.callback(
    Output('output_container', 'children'),
    Input('run_button', 'n_clicks'),
    State('upload-pgn', 'filename'),
    State('dd_player', 'value'),
    State('engine_depth', 'value'),
    State('book_depth', 'value'),
    State('undecided_thres', 'value'),
    State('lost_thres', 'value'),
)
def update_output(n_clicks, pgn_file, player, engine_depth, book_depth, undecided_thres, lost_thres):
    if n_clicks == 0:
        return ""
    spy = PGNSpy("tmp_" + pgn_file, player, book_depth, engine_depth, undecided_thres, lost_thres)
    df = spy.analyze_player()
    display_table = dash_table.DataTable(df.to_dict("records"), [{"name": i, "id": i} for i in df.columns])
    return [html.H2("Results"), display_table]


if __name__ == '__main__':
    app.run_server(debug=True)
