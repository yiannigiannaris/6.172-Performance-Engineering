def mult_dist(delta_fil, delta_rnk):
	return float(str(round(( 1.0 / ( delta_fil + 1 ) ) * ( 1.0 / ( delta_rnk + 1 ) ), 6)))

lookup = [[mult_dist(j, i) for i in range(10)] for j in range(10)]
print(lookup)