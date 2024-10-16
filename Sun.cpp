#include "Sun.h"
#include "SunScript.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <stack>
#include <filesystem>
#include <assert.h>

using namespace SunScript;

enum class TokenType
{
    OPEN_PARAN,
    CLOSE_PARAN,
    OPEN_BRACE,
    CLOSE_BRACE,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    SEMICOLON,
    COMMA,
    DOT,
    COLON,
    EQUALS,
    EQUALS_EQUALS,
    NOT_EQUALS,
    LESS_EQUALS,
    GREATER_EQUALS,
    AND,
    OR,
    NOT,
    LESS,
    GREATER,
    IDENTIFIER,
    INCREMENT,
    DECREMENT,
    PLUS_EQUALS,
    MINUS_EQUALS,
    STAR_EQUALS,
    SLASH_EQUALS,

    // Keywords

    IF,
    ELSE,
    FUNCTION,
    VAR,
    YIELD,
    RETURN,
    WHILE,
    FOR,

    // Literals

    STRING,
    NUMBER,
    INTEGER
};

class Token
{
public:
    Token(TokenType type, const std::string& value, int line)
        :
        _type(type),
        _value(value),
        _line(line)
    { }

    inline TokenType Type() const { return _type; }
    inline int Line() const { return _line; }
    inline std::string String() const { return _value; }
#if USE_SUN_FLOAT
    inline real Number() const { return std::strtof(_value.c_str(), 0); }
#else
    inline real Number() const { return std::strtod(_value.c_str(), 0); }
#endif
    inline int Integer() const { return std::strtol(_value.c_str(), 0, 10); }

private:
    TokenType _type;
    std::string _value;
    int _line;
};

//====================
// Scanner
//====================

class Scanner
{
public:
    Scanner();
    void ScanLine(const std::string& line);
    void AddToken(TokenType type, const std::string& value);
    void AddToken(TokenType type);
    inline bool IsError() const { return _isError; }
    inline const std::string& Error() const { return _error; }
    inline int ErrorLine() const { return _lineNum; }
    inline const std::vector<Token>& Tokens() const { return _tokens; }
private:
    char Peek();
    char PeekAhead();
    void Advance();
    void ScanWhitespace();
    void ScanStringLiteral();
    void ScanNumberLiteral();
    void ScanIdentifier();
    void SetError(const std::string& error);
    bool IsDigit(char ch);
    void AddKeyword(const std::string& keyword, TokenType token);
    bool IsLetter(char ch);

    std::string _line;
    int _pos;
    int _lineNum;
    bool _scanning;
    std::vector<Token> _tokens;
    std::string _error;
    bool _isError;
    std::unordered_map<std::string, TokenType> _keywords;
};

Scanner::Scanner()
    : _lineNum(1), _pos(0), _scanning(false), _isError(false)
{
    AddKeyword("if", TokenType::IF);
    AddKeyword("else", TokenType::ELSE);
    AddKeyword("function", TokenType::FUNCTION);
    AddKeyword("var", TokenType::VAR);
    AddKeyword("yield", TokenType::YIELD);
    AddKeyword("return", TokenType::RETURN);
    AddKeyword("while", TokenType::WHILE);
    AddKeyword("for", TokenType::FOR);
}

void Scanner::AddKeyword(const std::string& keyword, TokenType token)
{
    _keywords.insert(std::pair<std::string, TokenType>(keyword, token));
}

bool Scanner::IsLetter(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

void Scanner::ScanIdentifier()
{
    std::string identifier;
    bool scanning = true;
    while (scanning)
    {
        char ch = Peek();
        if (IsLetter(ch) || IsDigit(ch))
        {
            identifier += ch;
            Advance();
        }
        else
        {
            scanning = false;
        }
    }

    const auto& it = _keywords.find(identifier);
    if (it != _keywords.end())
    {
        AddToken(it->second);
    }
    else
    {
        AddToken(TokenType::IDENTIFIER, identifier);
    }
}

bool Scanner::IsDigit(char ch)
{
    bool isDigit = false;
    switch (ch)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        isDigit = true;
        break;
    }
    return isDigit;
}

void Scanner::SetError(const std::string& error)
{
    if (!_isError)
    {
        _isError = true;
        _error = error;
        _scanning = false;
    }
}

char Scanner::Peek()
{
    return _line[_pos];
}

char Scanner::PeekAhead()
{
    if (_pos + 1 < _line.size()) return _line[_pos + 1];
    else return '\0';
}

void Scanner::Advance()
{
    if (_scanning)
    {
        _pos++;
        _scanning = _pos < _line.size();
    }
}

void Scanner::ScanWhitespace()
{
    bool scanning = true;
    while (scanning && _scanning)
    {
        char ch = Peek();
        switch (ch)
        {
        case ' ':
        case '\t':
            Advance();
            break;
        default:
            scanning = false;
            break;
        }
    }
}

void Scanner::ScanStringLiteral()
{
    std::string str;
    bool scanning = true;
    while (scanning)
    {
        char ch = Peek();
        if (ch == '\"')
        {
            AddToken(TokenType::STRING, str);
            Advance();
            scanning = false;
        }
        else
        {
            str += ch;
            Advance();
            if (!_scanning)
            {
                SetError("Ill formed string literal.");
                scanning = false;
            }
        }
    }
}

void Scanner::ScanNumberLiteral()
{
    std::string str;
    bool scanning = true;
    bool hasDot = false;
    while (scanning)
    {
        char ch = Peek();
        if (IsDigit(ch))
        {
            str += ch;
            Advance();
        }
        else if (ch == '.')
        {
            if (!hasDot) {
                hasDot = true;
                str += ch;
                Advance();
            }
            else
            {
                SetError("Invalid numeric literal.");
                scanning = false;
            }
        }
        else
        {
            AddToken(hasDot ? TokenType::NUMBER : TokenType::INTEGER, str);
            scanning = false;
        }
    }
}

void Scanner::ScanLine(const std::string& line)
{
    if (_isError) { return; }

    _pos = 0;
    _line = line;
    _scanning = true;

    while (_scanning)
    {
        ScanWhitespace();

        char ch = Peek();

        switch (ch)
        {
            case '=':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::EQUALS_EQUALS); }
                else { AddToken(TokenType::EQUALS); }
                break;
            case '!':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::NOT_EQUALS); }
                else { AddToken(TokenType::NOT); }
                break;
            case '<':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::LESS_EQUALS); }
                else { AddToken(TokenType::LESS); }
                break;
            case '>':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::GREATER_EQUALS); }
                else { AddToken(TokenType::GREATER); }
                break;
            case ':':
                AddToken(TokenType::COLON);
                Advance();
                break;
            case ';':
                AddToken(TokenType::SEMICOLON);
                Advance();
                break;
            case ',':
                AddToken(TokenType::COMMA);
                Advance();
                break;
            case '(':
                AddToken(TokenType::OPEN_PARAN);
                Advance();
                break;
            case ')':
                AddToken(TokenType::CLOSE_PARAN);
                Advance();
                break;
            case '{':
                AddToken(TokenType::OPEN_BRACE);
                Advance();
                break;
            case '}':
                AddToken(TokenType::CLOSE_BRACE);
                Advance();
                break;
            case '+':
                Advance();
                if (Peek() == '+') { Advance(); AddToken(TokenType::INCREMENT); }
                else if (Peek() == '=') { Advance(); AddToken(TokenType::PLUS_EQUALS); }
                else { AddToken(TokenType::PLUS); }
                break;
            case '-':
                Advance();
                if (Peek() == '-') { Advance(); AddToken(TokenType::DECREMENT); }
                else if (Peek() == '=') { Advance(); AddToken(TokenType::MINUS_EQUALS); }
                else { AddToken(TokenType::MINUS); }
                break;
            case '*':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::STAR_EQUALS); }
                else { AddToken(TokenType::STAR); }
                break;
            case '/':
                Advance();
                if (Peek() == '/') { _scanning = false; } // comment
                else if (Peek() == '=') { Advance(); AddToken(TokenType::SLASH_EQUALS); }
                else { AddToken(TokenType::SLASH); }
                break;
            case '\"':
                Advance();
                ScanStringLiteral();
                break;
            case '&':
                Advance();
                if (Peek() == '&')
                {
                    AddToken(TokenType::AND);
                    Advance();
                }
                else
                {
                    SetError("Bitwise AND not supported.");
                }
                break;
            case '|':
                Advance();
                if (Peek() == '|')
                {
                    AddToken(TokenType::OR);
                    Advance();
                }
                else
                {
                    SetError("Bitwise OR not supported.");
                }
                break;
            default:
                if (IsDigit(ch))
                {
                    ScanNumberLiteral();
                }
                else if (IsLetter(ch))
                {
                    ScanIdentifier();
                }
                else if (ch == '.')
                {
                    if (IsDigit(PeekAhead()))
                    {
                        ScanNumberLiteral();
                    }
                    else
                    {
                        AddToken(TokenType::DOT);
                        Advance();
                    }
                }
                else if (ch == '\0')
                {
                    Advance();
                }
                else
                {
                    SetError("Unexcepted character.");
                }
                break;
        }
    }

    if (!IsError())
    {
        _lineNum++;
    }
}

void Scanner::AddToken(TokenType type, const std::string& value)
{
    Token token(type, value, _lineNum);
    _tokens.push_back(token);
}

void Scanner::AddToken(TokenType type)
{
    Token token(type, "", _lineNum);
    _tokens.push_back(token);
}

//===================
// Call
//===================

class Expr;

class Call
{
public:
    Call() : _yield(false), _discard(false) {};
    void PushArg(Expr* expr);
    inline std::vector<Expr*>& Args() { return _args; }
    inline void SetYield() { _yield = true; }
    inline bool Yield() const { return _yield; }
    inline void SetDiscard() { _discard = true; }
    inline bool Discard() { return _discard; }

private:
    std::vector<Expr*> _args;
    bool _yield;
    bool _discard;
};

void Call::PushArg(Expr* expr)
{
    _args.push_back(expr);
}

//====================
// Fold
//====================

class Fold
{
public:
    Fold() :
        _isNum(false),
        _isInt(false)
    {
        _value._integer = 0;
    }

    inline void SetFold(int value) {
        _value._integer = value;
        _isInt = true;
    }

    inline void SetFold(real value) {
        _value._number = value;
        _isNum = true;
    }

    inline bool IsInteger() const { return _isInt; }
    inline bool IsNumber() const { return _isNum; }
    inline int Integer() const { return _value._integer; }
    inline real Number() const { return _value._number; }

private:
    union
    {
        int _integer;
        real _number;
    } _value;

    bool _isInt;
    bool _isNum;
};

//===================
// Expr
//===================

class Expr
{
public:
    Expr(Expr* left, Expr* right, Token operation)
    :
        _left(left),
        _right(right),
        _operation(operation),
        _call(nullptr)
    {
    }

    inline Expr* Left() { return _left; }
    inline Expr* Right() { return _right; }
    inline Token Op() { return _operation; }
    inline Call* GetCall() { return _call; }
    inline void SetCall(Call* call) { _call = call; }
    inline Fold& GetFold() { return _fold; }

private:
    Expr* _left;
    Expr* _right;
    Call* _call;
    Token _operation;
    Fold _fold;
};

//====================
// FlowNode
//====================

class FlowNode
{
public:
    FlowNode(int id, Expr* expr);
    FlowNode(int id, Expr* expr, int success, int failure);
    inline int ID() { return _id; }
    inline Expr* Expression() { return _expr; }
    inline Label* GetLabel() { return &_label; }
    inline int Failure() { return _failure; }
    inline int Success() { return _success; }
    inline bool Emitted() { return _emitted; }
    inline void SetEmitted() { _emitted = true; }

private:
    Expr* _expr;
    Label _label;
    int _success;
    int _failure;
    int _id;
    int _emitted;
};

FlowNode::FlowNode(int id, Expr* expr)
    :
    _expr(expr),
    _success(-1),
    _failure(-1),
    _id(id),
    _emitted(false)
{
}

FlowNode::FlowNode(int id, Expr* expr, int success, int failure)
    :
    _expr(expr),
    _success(success),
    _failure(failure),
    _id(id),
    _emitted(false)
{
}

//====================
// FlowGraph
//====================

class FlowGraph
{
public:
    FlowGraph();

    void BuildFlowGraph(Expr* expr);

    inline Label* Failure() { return _nodes[_failure].GetLabel(); }
    inline Label* Success() { return _nodes[_success].GetLabel(); }
    inline FlowNode& Root() { return _nodes[_root]; }
    inline FlowNode& GetNode(int node) { return _nodes[node]; }

private:
    int CreateNode(Expr* expr);
    int CreateNode(Expr* expr, int failure, int success);

    int EXPR(Expr* expr, int success, int failure);
    int AND(Expr* expr, int success, int failure);
    int OR(Expr* expr, int success, int failure);

    int _success;
    int _failure;
    int _root;
    std::vector<FlowNode> _nodes;
};

FlowGraph::FlowGraph()
    :
    _root(-1),
    _failure(-1),
    _success(-1)
{
    _failure = CreateNode(nullptr);
    _success = CreateNode(nullptr);
}

int FlowGraph::CreateNode(Expr* expr)
{
    auto& node = _nodes.emplace_back(FlowNode(int(_nodes.size()), expr));
    return node.ID();
}

int FlowGraph::CreateNode(Expr* expr, int failure, int success)
{
    auto& node = _nodes.emplace_back(FlowNode(int(_nodes.size()), expr, success, failure));
    return node.ID();
}

void FlowGraph::BuildFlowGraph(Expr* expr)
{
    _root = EXPR(expr, _success, _failure);
}

int FlowGraph::EXPR(Expr* expr, int success, int failure)
{
    switch (expr->Op().Type())
    {
    case TokenType::AND:
        return AND(expr, success, failure);
    case TokenType::OR:
        return OR(expr, success, failure);
    case TokenType::EQUALS_EQUALS:
    case TokenType::NOT_EQUALS:
    case TokenType::LESS:
    case TokenType::GREATER:
    case TokenType::GREATER_EQUALS:
    case TokenType::LESS_EQUALS:
        return CreateNode(expr, failure, success);
    }

    return -1;
}

int FlowGraph::AND(Expr* expr, int success, int failure)
{
    // TODO: we may want to do the LEFT node first

    const int left = EXPR(expr->Left(), success, failure);
    const int right = EXPR(expr->Right(), left, failure);

    return right;
}

int FlowGraph::OR(Expr* expr, int success, int failure)
{
    // TODO: we may want to do the LEFT node first

    const int left = EXPR(expr->Left(), success, failure);
    const int right = EXPR(expr->Right(), success, left);

    return right;
}

//====================
// Parser
//====================

class Parser
{
public:
    Parser(const std::vector<Token>& tokens);
    void Parse();
    inline bool IsError() const { return _isError; }
    inline const std::string& Error() const { return _errorText; }
    inline int ErrorLine() const { return _errorLine; }
    inline Program* GetProgram() { return _program; }

private:

    struct Branch
    {
        FlowGraph graph;
        Label endLabel;
        Label startLabel;
    };

    struct Function
    {
        int id;
        ProgramBlock* blk;
    };

    struct StackFrame
    {
        bool _return;
        ProgramBlock* _block;
        std::unordered_map<std::string, int> _vars;
        std::stack<std::unordered_set<std::string>> _scope;

        StackFrame() : _return(false), _block(nullptr) {}
    };

    inline ProgramBlock* Block() { return _frames.top()._block; }

    Token Peek();
    void Advance();
    bool Match(TokenType type);
    void SetError(const std::string& text);
    void EmitExpr(Expr* expr);
    Expr* FoldExpr(Expr* expr);
    void EmitFlowGraph(FlowGraph& graph, ProgramBlock* program);
    bool EmitNode(FlowGraph& graph, FlowNode& node, ProgramBlock* program);
    void FreeExpr(Expr* expr);
    void ParseVar();
    void ParseWhile();
    void ParseFor();
    void ParseIfStatement();
    void ParseElse(Branch& prevBr);
    void ParseAssignmentStatement();
    Expr* ParseAssignment();
    void ParseFunction();
    void ParseReturn();
    void ParseYield();
    void ParseStatement();
    void ParseStatementBlock();
    void ParseParameter(std::vector<std::string>& params);
    Expr* ParseExprStatement();
    Expr* ParseExpression();
    Expr* ParseEquality();
    Expr* ParseLogicalAnd();
    Expr* ParseLogicalOr();
    Expr* ParseComparision();
    Expr* ParseTerm();
    Expr* ParseFactor();
    Expr* ParseUnary();
    Expr* ParsePrimary();
    Expr* ParseCall();
    Call* ParseArgument();
    char Flip(char jump);
    int GetOrCreateFunction(const std::string& name, ProgramBlock* blk);
    void PushScope();
    void PopScope();

    bool _scanning;
    int _pos;
    const std::vector<Token>& _tokens;
    bool _isError;
    std::string _errorText;
    int _errorLine;
    Program* _program;

    std::unordered_map<std::string, Function> _functions;
    std::stack<StackFrame> _frames;
};

Parser::Parser(const std::vector<Token>& tokens)
    : _tokens(tokens),
    _scanning(false),
    _pos(0),
    _isError(false),
    _errorLine(0)
{
    _program = CreateProgram();
    _frames.push(StackFrame());
    PushScope();
    _frames.top()._block = CreateProgramBlock(true, "main", 0);

    GetOrCreateFunction("main", _frames.top()._block);
}

void Parser::PushScope()
{
    assert(_frames.size() > 0);
    _frames.top()._scope.push(std::unordered_set<std::string>());
}

void Parser::PopScope()
{
    assert(_frames.size() > 0);

    auto& top = _frames.top();
    for (auto& item : top._scope.top())
    {
        top._vars.erase(item);
    }

    top._scope.pop();
}

int Parser::GetOrCreateFunction(const std::string& name, ProgramBlock* blk)
{
    int id = 0;
    const auto& func = _functions.find(name);
    if (func == _functions.end())
    {
        id = CreateFunction(_program);
        _functions.insert(std::pair<std::string, Function>(name, Function{ .id = id, .blk = blk }));
    }
    else
    {
        id = func->second.id;
        
        // Update the block if it is set
        if (blk) { func->second.blk = blk; }
    }

    return id;
}

char Parser::Flip(char jump)
{
    switch (jump)
    {
    case JUMP_E:
        return JUMP_NE;
    case JUMP_NE:
        return JUMP_E;
    case JUMP_G:
        return JUMP_LE;
    case JUMP_GE:
        return JUMP_L;
    case JUMP_L:
        return JUMP_GE;
    case JUMP_LE:
        return JUMP_G;
    }

    return JUMP;
}

bool Parser::EmitNode(FlowGraph& graph, FlowNode& node, ProgramBlock* program)
{
    if (node.Emitted()) { return false; }

    node.SetEmitted();

    EmitLabel(program, node.GetLabel());

    int jump = 0;
    switch (node.Expression()->Op().Type())
    {
    case TokenType::EQUALS_EQUALS:
        jump = JUMP_E;
        break;
    case TokenType::NOT_EQUALS:
        jump = JUMP_NE;
        break;
    case TokenType::LESS_EQUALS:
        jump = JUMP_LE;
        break;
    case TokenType::GREATER_EQUALS:
        jump = JUMP_GE;
        break;
    case TokenType::LESS:
        jump = JUMP_L;
        break;
    case TokenType::GREATER:
        jump = JUMP_G;
        break;
    }

    EmitExpr(node.Expression());

    bool result = false;

    auto& failure = graph.GetNode(node.Failure());
    auto& success = graph.GetNode(node.Success());

    if (!failure.Emitted())
    {
        if (failure.Expression())
        {
            EmitJump(program, jump, success.GetLabel());
            result = EmitNode(graph, failure, program);
        }
        else
        {
            EmitJump(program, Flip(jump), failure.GetLabel());
            result = true;
        }

        if (!success.Emitted() && success.Expression())
        {
            result = EmitNode(graph, success, program);
        }
    }
    else if (!success.Emitted())
    {
        if (success.Expression())
        {
            EmitJump(program, Flip(jump), failure.GetLabel());
            result = EmitNode(graph, success, program);
        }
        else
        {
            EmitJump(program, jump, success.GetLabel());
            result = false;
        }

        if (!failure.Emitted() && failure.Expression())
        {
            result = EmitNode(graph, failure, program);
        }
    }

    return result;
}

void Parser::EmitFlowGraph(FlowGraph& graph, ProgramBlock* program)
{
    const bool result = EmitNode(graph, graph.Root(), program);
    if (!result)
    {
        EmitJump(program, JUMP, graph.Failure());
    }

    EmitLabel(program, graph.Success());
}

Expr* Parser::FoldExpr(Expr* expr)
{
    assert(expr);

    const Token token = expr->Op();

    Expr* left = expr->Left();
    Expr* right = expr->Right();

    if (token.Type() == TokenType::INTEGER)
    {
        expr->GetFold().SetFold(token.Integer());
    }
    else if (token.Type() == TokenType::NUMBER)
    {
        expr->GetFold().SetFold(token.Number());
    }

    if (left && right)
    {
        // Binary operation
        
        left = FoldExpr(left);
        right = FoldExpr(right);

        switch (token.Type())
        {
        case TokenType::PLUS:
            if (left->GetFold().IsInteger() &&
                right->GetFold().IsInteger())
            {
                const int result = left->GetFold().Integer() + right->GetFold().Integer();
                expr->GetFold().SetFold(result);
            }
            else if (left->GetFold().IsNumber() &&
                right->GetFold().IsNumber())
            {
                const real result = left->GetFold().Number() + right->GetFold().Number();
                expr->GetFold().SetFold(result);
            }
            break;
        case TokenType::STAR:
            if (left->GetFold().IsInteger() &&
                right->GetFold().IsInteger())
            {
                const int result = left->GetFold().Integer() * right->GetFold().Integer();
                expr->GetFold().SetFold(result);
            }
            else if (left->GetFold().IsNumber() &&
                right->GetFold().IsNumber())
            {
                const real result = left->GetFold().Number() * right->GetFold().Number();
                expr->GetFold().SetFold(result);
            }
            break;
        case TokenType::MINUS:
            if (left->GetFold().IsInteger() &&
                right->GetFold().IsInteger())
            {
                const int result = left->GetFold().Integer() - right->GetFold().Integer();
                expr->GetFold().SetFold(result);
            }
            else if (left->GetFold().IsNumber() &&
                right->GetFold().IsNumber())
            {
                const real result = left->GetFold().Number() - right->GetFold().Number();
                expr->GetFold().SetFold(result);
            }
            break;
        case TokenType::SLASH:
            if (left->GetFold().IsInteger() &&
                right->GetFold().IsInteger())
            {
                const int result = left->GetFold().Integer() / right->GetFold().Integer();
                expr->GetFold().SetFold(result);
            }
            else if (left->GetFold().IsNumber() &&
                right->GetFold().IsNumber())
            {
                const real result = left->GetFold().Number() / right->GetFold().Number();
                expr->GetFold().SetFold(result);
            }
            break;
        }
    }
    else if (right)
    {
        // Unary operation
        
        right = FoldExpr(right);

        switch (token.Type())
        {
        case TokenType::MINUS:
            if (right->GetFold().IsInteger())
            {
                expr->GetFold().SetFold(-right->GetFold().Integer());
            }
            else if (right->GetFold().IsNumber())
            {
                expr->GetFold().SetFold(-right->GetFold().Number());
            }
            break;
        }
    }

    return expr;
}

void Parser::EmitExpr(Expr* expr)
{
    if (expr->GetFold().IsInteger())
    {
        auto& frame = _frames.top();
        ProgramBlock* block = frame._block;
        EmitPush(block, expr->GetFold().Integer());
        return;
    }
    else if (expr->GetFold().IsNumber())
    {
        auto& frame = _frames.top();
        ProgramBlock* block = frame._block;
        EmitPush(block, expr->GetFold().Number());
        return;
    }

    if (expr->Left())
    {
        EmitExpr(expr->Left());
    }

    if (expr->Right())
    {
        EmitExpr(expr->Right());
    }

    auto& frame = _frames.top();
    ProgramBlock* block = frame._block;
    const bool binary = expr->Left() && expr->Right();

    Token tok = expr->Op();
    switch (tok.Type())
    {
    case TokenType::EQUALS_EQUALS:
        EmitCompare(block);
        break;
    case TokenType::NOT_EQUALS:
        EmitCompare(block);
        break;
    case TokenType::INCREMENT:
        EmitIncrement(block);
        break;
    case TokenType::DECREMENT:
        EmitDecrement(block);
        break;
    case TokenType::PLUS:
        EmitAdd(block);
        break;
    case TokenType::STAR:
        EmitMul(block);
        break;
    case TokenType::MINUS:
        if (binary)
        {
            EmitSub(block);
        }
        else
        {
            EmitUnaryMinus(block);
        }
        break;
    case TokenType::SLASH:
        EmitDiv(block);
        break;
    case TokenType::GREATER:
        EmitCompare(block);
        break;
    case TokenType::LESS:
        EmitCompare(block);
        break;
    case TokenType::GREATER_EQUALS:
        EmitCompare(block);
        break;
    case TokenType::LESS_EQUALS:
        EmitCompare(block);
        break;
    case TokenType::STRING:
        EmitPush(block, tok.String());
        break;
    case TokenType::NUMBER:
        EmitPush(block, tok.Number());
        break;
    case TokenType::INTEGER:
        EmitPush(block, tok.Integer());
        break;
    case TokenType::IDENTIFIER:
        if (expr->GetCall())
        {
            Call* call = expr->GetCall();
            auto& args = call->Args();
            for (int i = int(args.size()) - 1; i >= 0; i--)
            {
                EmitExpr(args[i]);
            }

            const int id = GetOrCreateFunction(tok.String(), nullptr);

            if (call->Yield())
            {
                EmitDebug(block, tok.Line());
                EmitYield(block, id, static_cast<unsigned char>(args.size()));
            }
            else if (call->Discard())
            {
                EmitDebug(block, tok.Line());
                EmitCallX(block, id, static_cast<unsigned char>(args.size()));
            }
            else
            {
                EmitDebug(block, tok.Line());
                EmitCall(block, id, static_cast<unsigned char>(args.size()));
            }
        }
        else
        {
            const auto& it = frame._vars.find(tok.String());
            if (it != frame._vars.end())
            {
                EmitDebug(block, tok.Line());
                EmitPushLocal(block, it->second);
            }
            else
            {
                SetError("Use of undefined variable '" + tok.String() + "'.");
            }
        }
        break;
    default:
        SetError("Unexpected token '" + tok.String() + "' emitting expression.");
        break;
    }
}

void Parser::FreeExpr(Expr* expr)
{
    if (expr->Right())
    {
        FreeExpr(expr->Right());
    }

    if (expr->Left())
    {
        FreeExpr(expr->Left());
    }

    if (expr->GetCall())
    {
        delete expr->GetCall();
        expr->SetCall(nullptr);
    }

    delete expr;
}

void Parser::SetError(const std::string& text)
{
    if (!_isError)
    {
        _isError = true;
        _errorText = text;
        _scanning = false;
        _errorLine = _tokens[std::min(int(_tokens.size()) - 1, _pos)].Line();
    }
}

Token Parser::Peek()
{
    return _tokens[_pos];
}

void Parser::Advance()
{
    if (_scanning)
    {
        _pos++;
        _scanning = _pos < _tokens.size();
    }
}

bool Parser::Match(TokenType type)
{
    return _scanning && Peek().Type() == type;
}

void Parser::ParseYield()
{
    Advance();

    Expr* expr = ParseCall();
    if (expr)
    {
        Call* call = expr->GetCall();
        if (call)
        {
            call->SetYield();

            EmitExpr(expr);
            FreeExpr(expr);

            if (Match(TokenType::SEMICOLON))
            {
                Advance();
            }
            else
            {
                SetError("Unexpected token.");
            }
        }
        else
        {
            SetError("Unexcepted token.");
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseReturn()
{
    if (_frames.size() > 1)
    {
        if (Match(TokenType::RETURN))
        {
            Advance();
            Expr* expr = ParseExprStatement();
            if (expr)
            {
                EmitExpr(expr);
                FreeExpr(expr);

                EmitReturn(Block());
            }
            else
            {
                EmitReturn(Block());
            }

            // If there is a top level return record it.
            if (_frames.top()._scope.size() == 1)
            {
                _frames.top()._return = true;
            }
        }
    }
    else
    {
        SetError("Unexpected return statement.");
    }
}

void Parser::ParseParameter(std::vector<std::string>& params)
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token tok = Peek();
        Advance();

        params.push_back(tok.String());

        while (Match(TokenType::COMMA))
        {
            Advance();
            if (Match(TokenType::IDENTIFIER))
            {
                Token tok = Peek();
                Advance();

                params.push_back(tok.String());
            }
            else
            {
                SetError("Missing parameter identifier.");
                break;
            }
        }
    }
}

void Parser::ParseFunction()
{
    if (Match(TokenType::FUNCTION))
    {
        Advance();

        ProgramBlock* block = nullptr;
        std::vector<std::string> params;

        if (Match(TokenType::IDENTIFIER))
        {
            Token token = Peek();
            Advance();
        
            if (Match(TokenType::OPEN_PARAN))
            {
                Advance();

                ParseParameter(params);

                block = CreateProgramBlock(false, token.String(), int(params.size()));

                const int id = GetOrCreateFunction(token.String(), block);
                for (int i = 0; i < int(params.size()); i++)
                {
                    auto& param = params[i];
                    EmitLocal(block, param);
                    EmitPop(block, i);
                    EmitParameter(block, param);
                }
            }
            else
            {
                SetError("Unexpected token, expected '('");
            }
        }
        else
        {
            SetError("Unexpected token, expected the name of the function.");
        }
    
        if (!IsError())
        {
            if (Match(TokenType::CLOSE_PARAN))
            {
                Advance();

                if (Match(TokenType::OPEN_BRACE))
                {
                    Advance();
                    _frames.push(StackFrame());
                    auto& top = _frames.top();
                    PushScope();
                    top._block = block;

                    for (int i = 0; i < params.size(); i++)
                    {
                        auto& param = params[i];
                        top._vars.insert(std::pair<std::string, int>(param, i));
                    }

                    while (!Match(TokenType::CLOSE_BRACE))
                    {
                        ParseStatement();
                    }
                    Advance();

                    // If there is no top level return insert one.
                    if (!_frames.top()._return)
                    {
                        EmitReturn(Block());
                    }

                    EmitProgramBlock(_program, Block());
                    _frames.pop();
                }
                else
                {
                    ReleaseProgramBlock(block);
                    SetError("Unexpected token, expected '{'");
                }
            }
            else
            {
                ReleaseProgramBlock(block);
                SetError("Unexpected token, expected ')'");
            }
        }
    }
}

Expr* Parser::ParseCall()
{
    Expr* expr = ParsePrimary();
    if (Match(TokenType::OPEN_PARAN))
    {
        Advance();

        Call* call = ParseArgument();
        expr->SetCall(call);

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
    }
    return expr;
}

Call* Parser::ParseArgument()
{
    Call* call = new Call();
    Expr* expr = ParseExpression();
    if (expr)
    {
        call->PushArg(expr);
        while (Match(TokenType::COMMA))
        {
            Advance();
            call->PushArg(ParseExpression());
        }
    }
    return call;
}

Expr* Parser::ParsePrimary()
{
    if (Match(TokenType::STRING))
    {
        Token str = Peek();
        Advance();
        return new Expr(nullptr, nullptr, str);
    }
    else if (Match(TokenType::NUMBER))
    {
        Token number = Peek();
        Advance();
        return new Expr(nullptr, nullptr, number);
    }
    else if (Match(TokenType::INTEGER))
    {
        Token number = Peek();
        Advance();
        return new Expr(nullptr, nullptr, number);
    }
    else if (Match(TokenType::IDENTIFIER))
    {
        Token id = Peek();
        Advance();
        return new Expr(nullptr, nullptr, id);
    }
    else if (Match(TokenType::OPEN_PARAN))
    {
        Advance();
        Expr* expr = ParseExpression();

        bool cont = Match(TokenType::CLOSE_PARAN);
        if (cont)
        {
            Advance();
            return expr;
        }
        else
        {
            delete expr;
            SetError("Unexpected token.");
        }
    }

    return nullptr;
}

Expr* Parser::ParseUnary()
{
    if (Match(TokenType::MINUS) || Match(TokenType::NOT))
    {
        Token op = Peek();
        Advance();
        Expr* right = ParseUnary();
        return new Expr(nullptr, right, op);
    }

    return ParseCall();
}

Expr* Parser::ParseFactor()
{
    Expr* expr = ParseUnary();

    while (Match(TokenType::SLASH) || Match(TokenType::STAR))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseUnary(), op);
    }

    return expr;
}

Expr* Parser::ParseTerm()
{
    Expr* expr = ParseFactor();

    while (Match(TokenType::PLUS) || Match(TokenType::MINUS))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseFactor(), op);
    }

    return expr;
}

Expr* Parser::ParseLogicalAnd()
{
    Expr* expr = ParseEquality();

    while (Match(TokenType::AND))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseEquality(), op);
    }

    return expr;
}

Expr* Parser::ParseLogicalOr()
{
    Expr* expr = ParseLogicalAnd();

    while (Match(TokenType::OR))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseLogicalAnd(), op);
    }

    return expr;
}

Expr* Parser::ParseComparision()
{
    Expr* expr = ParseTerm();

    while (Match(TokenType::GREATER) || Match(TokenType::GREATER_EQUALS) || Match(TokenType::LESS) || Match(TokenType::LESS_EQUALS))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseTerm(), op);
    }

    return expr;
}

Expr* Parser::ParseEquality()
{
    Expr* expr = ParseComparision();
    while (Match(TokenType::EQUALS_EQUALS) || Match(TokenType::NOT_EQUALS))
    {
        Token equals = Peek();
        Advance();
        Expr* right = ParseComparision();
        expr = new Expr(expr, right, equals);
    }

    return expr;
}

Expr* Parser::ParseExpression()
{
    return ParseLogicalOr();
}

void Parser::ParseIfStatement()
{
    Expr* expr = nullptr;
    if (Match(TokenType::OPEN_PARAN))
    {
        Token token = Peek();
        Advance();
        expr = ParseExpression();

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }

        if (!IsError() && Match(TokenType::OPEN_BRACE))
        {
            Advance();

            Branch br;

            br.graph.BuildFlowGraph(FoldExpr(expr));
            EmitFlowGraph(br.graph, Block());

            FreeExpr(expr);

            PushScope();
            ParseStatementBlock();
            PopScope();

            ParseElse(br);
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}
 
Expr* Parser::ParseExprStatement()
{
    Expr* expr = ParseExpression();
    if (Match(TokenType::SEMICOLON))
    {
        Advance();
        return expr;
    }
    else
    {
        SetError("Unexpected token occurred parsing statement.");
    }
    return expr;
}

void Parser::ParseAssignmentStatement()
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token identifier = Peek();
        StackFrame& frame = _frames.top();
        const auto& var = frame._vars.find(identifier.String());
        if (var != frame._vars.end())
        {
            Expr* expr = ParseAssignment();

            if (Match(TokenType::SEMICOLON))
            {
                Advance();

                EmitExpr(expr);
                EmitPop(Block(), var->second);
            }
            else
            {
                SetError("Unexpected token.");
            }

            FreeExpr(expr);
        }
        else
        {
            Expr* expr = ParseCall();
            if (expr)
            {
                Call* call = expr->GetCall();
                if (call)
                {
                    call->SetDiscard();

                    EmitExpr(expr);
                    FreeExpr(expr);
                }
                else
                {
                    SetError("Unexpected token.");
                }
            }

            if (Match(TokenType::SEMICOLON))
            {
                Advance();
            }
            else
            {
                SetError("Unexpected token.");
            }
        }
    }
}

Expr* Parser::ParseAssignment()
{
    Token identifier = Peek();
    Advance();
    
    const auto& it = _frames.top()._vars.find(identifier.String());

    if (it == _frames.top()._vars.end())
    {
        SetError("Undefined variable '" + identifier.String() + "'");
        return nullptr;
    }

    Token op = Peek();
    Expr* expr = nullptr;
    if (Match(TokenType::EQUALS))
    {
        Advance();
        expr = ParseExpression();
    }
    else if (Match(TokenType::INCREMENT))
    {
        Advance();
        expr = new Expr(new Expr(nullptr, nullptr, identifier), nullptr, op);
    }
    else if (Match(TokenType::DECREMENT))
    {
        Advance();
        expr = new Expr(new Expr(nullptr, nullptr, identifier), nullptr, op);
    }
    else if (Match(TokenType::PLUS_EQUALS) ||
        Match(TokenType::MINUS_EQUALS) ||
        Match(TokenType::STAR_EQUALS) ||
        Match(TokenType::SLASH_EQUALS))
    {
        Advance();

        TokenType type;
        switch (op.Type())
        {
        case TokenType::MINUS_EQUALS:
            type = TokenType::MINUS;
            break;
        case TokenType::PLUS_EQUALS:
            type = TokenType::PLUS;
            break;
        case TokenType::STAR_EQUALS:
            type = TokenType::STAR;
            break;
        case TokenType::SLASH_EQUALS:
            type = TokenType::SLASH;
            break;
        }

        expr = new Expr(new Expr(nullptr, nullptr, identifier), ParseExpression(), Token(type, op.String(), op.Line()));
    }
    else
    {
        SetError("Unexcepted token.");
    }

    return expr;
}

void Parser::ParseVar()
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token identifier = Peek();
        Advance();

        StackFrame& top = _frames.top();
        if (top._vars.find(identifier.String()) == top._vars.end())
        {
            if (Match(TokenType::EQUALS))
            {
                Advance();

                Expr* expr = ParseExpression();

                if (Match(TokenType::SEMICOLON))
                {
                    const int var = int(top._vars.size());

                    EmitExpr(FoldExpr(expr));
                    EmitLocal(Block(), identifier.String());
                    EmitPop(Block(), var);
                    FreeExpr(expr);

                    StackFrame& frame = _frames.top();
                    frame._vars.insert(std::pair<std::string, int>(identifier.String(), var));
                    frame._scope.top().insert(identifier.String());

                    Advance();
                }
                else
                {
                    SetError("Unexcepted token.");
                }
            }
            else if (Match(TokenType::SEMICOLON))
            {
                Advance();
                EmitLocal(Block(), identifier.String());

                const int var = int(top._vars.size());

                StackFrame& frame = _frames.top();
                frame._vars.insert(std::pair<std::string, int>(identifier.String(), var));
                frame._scope.top().insert(identifier.String());
            }
            else
            {
                SetError("Unexcepted token.");
            }
        }
        else
        {
            SetError("Redefinition of variable " + identifier.String());
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseElse(Branch& prevBr)
{
    if (Match(TokenType::ELSE))
    {
        Advance();

        // After the IF body is executed jump to the end.
        EmitJump(Block(), JUMP, &prevBr.endLabel);

        if (Match(TokenType::OPEN_BRACE))
        {
            Advance();

            Branch br;
            br.endLabel = prevBr.endLabel;

            EmitLabel(Block(), prevBr.graph.Failure());

            PushScope();
            ParseStatementBlock();
            PopScope();

            EmitLabel(Block(), br.graph.Failure());
            EmitLabel(Block(), &br.endLabel); // end if
        }
        else if (Match(TokenType::IF))
        {
            Advance();

            EmitLabel(Block(), prevBr.graph.Failure());
            Label label = prevBr.endLabel;

            ParseIfStatement();

            // Propagate the end labels.
            auto& end = prevBr.endLabel;
            end.jumps.insert(
                end.jumps.end(),
                label.jumps.begin(),
                label.jumps.end());
        }
        else
        {
            SetError("Unexpected token after ELSE clause.");
        }
    }
    else
    {
        EmitLabel(Block(), prevBr.graph.Failure());
        EmitLabel(Block(), &prevBr.endLabel); // end if
    }
}

void Parser::ParseFor()
{
    if (Match(TokenType::OPEN_PARAN))
    {
        Token token = Peek();
        Advance();

        PushScope();

        if (Match(TokenType::VAR))
        {
            Advance();
            ParseVar();
        }
        else if (Match(TokenType::SEMICOLON))
        {
            Advance();
        }
        else
        {
            SetError("Unexpected token.");
        }

        Expr* second = ParseExpression();

        if (Match(TokenType::SEMICOLON))
        {
            Advance();
        }
        else
        {
            SetError("Unexpected token.");
        }

        Token left = Peek();
        Expr* third = ParseAssignment();

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }

        if (!IsError() && Match(TokenType::OPEN_BRACE))
        {
            Advance();

            // Before loop body

            Branch br;
            MarkLabel(Block(), &br.startLabel);

            if (second)
            {
                br.graph.BuildFlowGraph(second);
                EmitFlowGraph(br.graph, Block());
            }

            // Loop body
            ParseStatementBlock();

            // After loop
            if (third)
            {
                auto& frame = _frames.top();

                EmitExpr(third);
                EmitPop(Block(), frame._vars[left.String()]);
                FreeExpr(third);
            }

            PopScope();

            SunScript::ProgramBlock* prog = Block();

            EmitJump(prog, JUMP, &br.startLabel);   // Unconditionally jump to start of loop.
            EmitMarkedLabel(prog, &br.startLabel);
            EmitLabel(prog, &br.endLabel);

            if (second)
            {
                EmitLabel(prog, br.graph.Failure());
                FreeExpr(second);
            }
        }
    }
    else
    {
        SetError("Unexpected token.");
    }
}

void Parser::ParseWhile()
{
    Expr* expr = nullptr;
    if (Match(TokenType::OPEN_PARAN))
    {
        Token token = Peek();
        Advance();
        expr = ParseExpression();

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }

        if (!IsError() && Match(TokenType::OPEN_BRACE))
        {
            Advance();

            Branch br;
            MarkLabel(Block(), &br.startLabel);

            br.graph.BuildFlowGraph(expr);
            EmitFlowGraph(br.graph, Block());
            FreeExpr(expr);

            PushScope();
            ParseStatementBlock();
            PopScope();

            SunScript::ProgramBlock* prog = Block();

            EmitJump(prog, JUMP, &br.startLabel);   // Unconditionally jump to start of loop.
            EmitMarkedLabel(prog, &br.startLabel);
            EmitLabel(prog, &br.endLabel);
            EmitLabel(prog, br.graph.Failure());
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseStatementBlock()
{
    while (_scanning && !Match(TokenType::CLOSE_BRACE))
    {
        ParseStatement();
    }
    if (!Match(TokenType::CLOSE_BRACE))
    {
        SetError("Expected close brace.");
    }
    Advance();
}

void Parser::ParseStatement()
{
    Token token = Peek();
    switch (token.Type())
    {
    case TokenType::WHILE:
        Advance();
        ParseWhile();
        break;
    case TokenType::FOR:
        Advance();
        ParseFor();
        break;
    case TokenType::IF:
        Advance();
        ParseIfStatement();
        break;
    case TokenType::VAR:
        Advance();
        ParseVar();
        break;
    case TokenType::FUNCTION:
        ParseFunction();
        break;
    case TokenType::RETURN:
        ParseReturn();
        break;
    case TokenType::YIELD:
        ParseYield();
        break;
    case TokenType::IDENTIFIER:
        ParseAssignmentStatement();
        break;
    default:
        SetError("Unexpected token.");
        break;
    }
}

void Parser::Parse()
{
    _scanning = true;

    while (_scanning)
    {
        ParseStatement();
    }

    if (!_isError)
    {
        EmitDone(Block());
        EmitProgramBlock(_program, Block());

        for (auto& func : _functions)
        {
            if (func.second.blk)
            {
                EmitInternalFunction(_program, func.second.blk, func.second.id);
            }
            else
            {
                EmitExternalFunction(_program, func.second.id, func.first);
            }
        }
        FlushBlocks(_program);
    }

    for (auto& func : _functions)
    {
        if (func.second.blk)
        {
            ReleaseProgramBlock(func.second.blk);
        }
    }
}

//====================

static SunScript::Program* Compile(Scanner& scanner, std::string* error)
{
    SunScript::Program* program = nullptr;

    if (scanner.IsError())
    {
        if (error)
        {
            std::stringstream ss;
            ss << "Error Line: " << scanner.ErrorLine() << " " << scanner.Error();

            *error = ss.str();
        }
    }
    else
    {
        Parser parser(scanner.Tokens());
        parser.Parse();

        if (!parser.IsError())
        {
            program = parser.GetProgram();
        }
        else if (error)
        {
            std::stringstream ss;
            ss << "Error Line: " << parser.ErrorLine() << " " << parser.Error();

            *error = ss.str();
        }
    }

    return program;
}

void SunScript::CompileText(const std::string& scriptText,
    unsigned char** programData, unsigned char** debugData,
    int* programSize, int* debugSize, std::string* error)
{
    SunScript::Program* program = nullptr;

    Scanner scanner;
    std::stringstream ss(scriptText);

    const int size = 512;
    char line[size];

    while (!ss.eof())
    {
        ss.getline(line, size);
        scanner.ScanLine(line);
    }

    program = Compile(scanner, error);

    if (program)
    {
        *programSize = GetProgram(program, programData);

        if (debugData)
        {
            *debugSize = GetDebugData(program, debugData);
        }
    }
}

static SunScript::Program* CompileFile2(const std::string& filepath, std::string* error)
{
    SunScript::Program* program = nullptr;

    std::ifstream stream;
    stream.open(filepath);
    if (stream.good())
    {
        const int size = 512;
        char line[size];

        Scanner scanner;

        while (!stream.eof())
        {
            stream.getline(line, size);
            scanner.ScanLine(line);
        }

        program = Compile(scanner, error);
    }
    else
    {
        std::stringstream ss;
        ss << "File not found.";

        *error = ss.str();
    }

    return program;
}

void SunScript::CompileFile(const std::string& filepath, unsigned char** programData, int* programSize)
{
    CompileFile(filepath, programData, nullptr, programSize, nullptr, nullptr);
}

void SunScript::CompileFile(const std::string& filepath,
    unsigned char** programData, unsigned char** debugData,
    int* programSize, int* debugSize,
    std::string* error)
{
    Program* program = CompileFile2(filepath, error);

    *programData = nullptr;

    if (program)
    {
        *programSize = GetProgram(program, programData);

        if (debugData)
        {
            *debugSize = GetDebugData(program, debugData);
        }
    }
}

//==========================
// Sun compiler
//==========================

#ifdef _SUN_EXECUTABLE_
#include <iostream>
#include <cstring>
#include "SunScriptDemo.h"
#include "Tests/SunTest.h"
    static void PrintHelp()
    {
        std::cout << "Usage:" << std::endl;
        std::cout << "Sun build <file1> <file2>..." << std::endl;
        std::cout << "Sun disassemble <file1>" << std::endl;
        std::cout << "Sun demo" << std::endl;
    }

    static void Build(int numFiles, char** files)
    {
        for (int i = 0; i < numFiles; i++)
        {
            std::string filename = files[i];
            std::cout << "[" << filename << "] ";

            std::string error;
            
            Program* program = CompileFile2(filename, &error);
            if (program)
            {
                unsigned char* programData;
                unsigned char* debugData;
                const int versionNumber = 0;
                const int programDataSize = GetProgram(program, &programData);
                const int debugDataSize   = GetDebugData(program, &debugData);

                std::filesystem::path path = filename;
                path.replace_extension("obj");

                std::ofstream ss;
                ss.open(path, std::ios::binary | std::ios::trunc);
                ss.write((char*)&versionNumber, sizeof(int));
                ss.write((char*)&programDataSize, sizeof(int));
                ss.write((char*)programData, programDataSize);
                ss.write((char*)&debugDataSize, sizeof(int));
                ss.write((char*)debugData, debugDataSize);
                ss.close();

                std::cout << "Script built successfully" << std::endl;
            }
            else
            {
                std::cout << "Failed to compile script: " << error << std::endl;
            }
        }
    }

    static void DisassembleProgram(const std::string& file)
    {
        unsigned char* program;
        LoadScript(file, &program);
        std::cout << "Script loaded" << std::endl;
        if (program)
        {
            std::stringstream ss;
            Disassemble(ss, program, nullptr);
            std::cout << ss.str();

            delete[] program;
        }
        else
        {
            std::cout << "Failed to load program." << std::endl;
        }
    }

    int GetOpts(int numArgs, char** args)
    {
        int opt = OPT_NONE;
        for (int i = 0; i < numArgs; i++)
        {
            if (std::strcmp(args[i], "--trace") == 0)
            {
                opt |= OPT_DUMPTRACE;
                std::cout << "Dumping trace: on" << std::endl;
            }
        }
        return opt;
    }

    int main(int numArgs, char** args)
    {
        if (numArgs <= 1)
        {
            PrintHelp();
        }
        else
        {
            std::cout << "SunScript Compiler" << std::endl;

            std::string cmd = args[1];
            if (cmd == "build")
            {
                Build(numArgs - 2, args + 2);
            }
            else if (cmd == "disassemble")
            {
                if (numArgs <= 2)
                {
                    std::cout << "No program data specified." << std::endl;
                }
                else
                {
                    DisassembleProgram(args[2]);
                }
            }
            else if (cmd == "test")
            {
                const int opts = GetOpts(numArgs, args);
                if (numArgs <= 2)
                {
                    RunTestSuite(".", opts);
                }
                else
                {
                    RunTestSuite(args[2], opts);
                }
            }
            else if (cmd == "demo")
            {
                std::cout << "Demos:" << std::endl;
                std::cout << "sun demo1" << std::endl;
                std::cout << "sun demo2" << std::endl;
                std::cout << "sun demo3" << std::endl;
                std::cout << "sun demo4" << std::endl;
                std::cout << "sun demo5" << std::endl;
                std::cout << "sun demo6" << std::endl;
            }
            else if (cmd == "demo1")
            {
                std::cout << "Running Demo1()" << std::endl;
                SunScript::Demo1(42);
            }
            else if (cmd == "demo2")
            {
                std::cout << "Running Demo2()" << std::endl;
                SunScript::Demo2();
            }
            else if (cmd == "demo3")
            {
                std::cout << "Running Demo3()" << std::endl;
                SunScript::Demo3();
            }
            else if (cmd == "demo4")
            {
                std::cout << "Running Demo4()" << std::endl;
                SunScript::Demo4();
            }
            else if (cmd == "demo5")
            {
                std::cout << "Running Demo5()" << std::endl;
                SunScript::Demo5();
            }
            else if (cmd == "demo6")
            {
                std::cout << "Running Demo6()" << std::endl;
                SunScript::Demo6();
            }
            else
            {
                std::cout << "Invalid command." << std::endl;
            }
        }
        
        return 0;
    }
#endif

//=====================
