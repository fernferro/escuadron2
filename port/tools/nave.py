# Nave propia del proyecto (vista de arriba, punta hacia arriba), en coordenadas -1..1.
# La usan el icono (port/tools/icono.py) y estilo.h (misma forma, copiada a mano).
CUERPO = [(0,-1),(0.11,-0.62),(0.15,0.55),(0.09,0.86),(-0.09,0.86),(-0.15,0.55),(-0.11,-0.62)]
ALA_D  = [(0.13,-0.02),(0.92,0.42),(0.98,0.62),(0.14,0.46)]
ALETA_D= [(0.82,0.30),(0.98,0.08),(1.0,0.66),(0.9,0.62)]
CANARD_D=[(0.1,-0.42),(0.36,-0.2),(0.34,-0.12),(0.11,-0.24)]
CABINA = [(0,-0.66),(0.06,-0.42),(0,-0.3),(-0.06,-0.42)]
MOTORES= [(-0.065,0.88),(0.065,0.88)]
def espejo(p): return [(-x,y) for x,y in p]
